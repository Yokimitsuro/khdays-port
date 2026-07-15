#include "khdays/assets/mesh.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Neutral-mesh decoder for Nintendo DS MDL0 geometry.
//
// This decoder executes the model's render command stream (SBC) the way the DS
// does: it drives a small matrix-stack virtual machine that builds a palette of
// rest-pose matrices from the model's bones (objects), inverse-bind matrices,
// and skinning equations, then draws each piece's packed GPU command stream.
// Every vertex keeps its raw local position and the palette index of the matrix
// that transforms it to model space, so the runtime can re-pose the model for
// animation instead of relying on a baked-in rest pose.
//
// The binary formats (bone TRS, pivot rotations, inverse binds, SBC opcodes,
// and GPU vertex commands) follow the Nintendo DS documentation and the public
// understanding captured by the apicula project; the implementation here is an
// independent reimplementation.

namespace {

using ByteVector = std::vector<std::uint8_t>;
using Matrix = std::array<float, 16>;  // column-major: m[col * 4 + row]

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

// ----- Fixed-point helpers -------------------------------------------------

std::int32_t sign_extend(const std::uint32_t value, const unsigned bits) {
    const std::uint32_t mask = (1U << bits) - 1U;
    const std::uint32_t masked = value & mask;
    const std::uint32_t sign_bit = 1U << (bits - 1U);
    if ((masked & sign_bit) != 0U) {
        return static_cast<std::int32_t>(masked | ~mask);
    }
    return static_cast<std::int32_t>(masked);
}

// Signed fixed-point with 12 fractional bits (the common Nitro format).
float fix_1_x_12(const std::int32_t raw) {
    return static_cast<float>(raw) / 4096.0F;
}

// ----- Matrix math (column-major, point transform p' = M * p) --------------

Matrix matrix_identity() {
    return Matrix{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
}

Matrix matrix_zero() {
    Matrix result{};
    result.fill(0.0F);
    return result;
}

Matrix matrix_multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (int k = 0; k < 4; ++k) {
                sum += a[static_cast<std::size_t>(k * 4 + row)]
                    * b[static_cast<std::size_t>(col * 4 + k)];
            }
            result[static_cast<std::size_t>(col * 4 + row)] = sum;
        }
    }
    return result;
}

void matrix_add_scaled(Matrix& accumulator, const Matrix& value, const float weight) {
    for (std::size_t i = 0U; i < 16U; ++i) {
        accumulator[i] += value[i] * weight;
    }
}

Matrix matrix_scale(const float x, const float y, const float z) {
    Matrix result = matrix_identity();
    result[0] = x;
    result[5] = y;
    result[10] = z;
    return result;
}

std::array<float, 3> matrix_transform_point(
    const Matrix& m,
    const std::array<float, 3>& p) {
    std::array<float, 3> out{};
    for (int i = 0; i < 3; ++i) {
        const auto row = static_cast<std::size_t>(i);
        out[row] =
            m[static_cast<std::size_t>(0 * 4 + i)] * p[0]
            + m[static_cast<std::size_t>(1 * 4 + i)] * p[1]
            + m[static_cast<std::size_t>(2 * 4 + i)] * p[2]
            + m[static_cast<std::size_t>(3 * 4 + i)];
    }
    return out;
}

// ----- Bone (object) transforms --------------------------------------------

// Build a 3x3 pivot rotation (stored column-major inside a 4x4). Mirrors the
// Nintendo DS compressed rotation form.
Matrix pivot_matrix(
    const unsigned select,
    const unsigned neg,
    const float a,
    const float b) {
    const float o = (neg & 0x01U) == 0U ? 1.0F : -1.0F;
    const float c = (neg & 0x02U) == 0U ? b : -b;
    const float d = (neg & 0x04U) == 0U ? a : -a;

    // Each case lists the 3x3 column-major (c0r0 c0r1 c0r2 c1r0 ...), matching
    // the Nintendo DS / cgmath layout used as the reference.
    std::array<float, 9> r{};
    switch (select) {
        case 0: r = {o, 0, 0, 0, a, b, 0, c, d}; break;
        case 1: r = {0, o, 0, a, 0, b, c, 0, d}; break;
        case 2: r = {0, 0, o, a, b, 0, c, d, 0}; break;
        case 3: r = {0, a, b, o, 0, 0, 0, c, d}; break;
        case 4: r = {a, 0, b, 0, o, 0, c, 0, d}; break;
        case 5: r = {a, b, 0, 0, 0, o, c, d, 0}; break;
        case 6: r = {0, a, b, 0, c, d, o, 0, 0}; break;
        case 7: r = {a, 0, b, c, 0, d, 0, o, 0}; break;
        case 8: r = {a, b, 0, c, d, 0, 0, 0, o}; break;
        default: r = {-a, 0, 0, 0, 0, 0, 0, 0, 0}; break;
    }

    Matrix m = matrix_identity();
    // r is column-major; copy directly (m[col*4 + row] = r[col*3 + row]).
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            m[static_cast<std::size_t>(col * 4 + row)] =
                r[static_cast<std::size_t>(col * 3 + row)];
        }
    }
    return m;
}

Matrix read_object_matrix(const ByteVector& data, const std::size_t offset) {
    const auto flags = read_u16(data, offset, "object flags");
    const bool has_no_trans = (flags & 0x01U) != 0U;   // t
    const bool has_no_rot = (flags & 0x02U) != 0U;     // r
    const bool has_no_scale = (flags & 0x04U) != 0U;   // s
    const bool has_pivot = (flags & 0x08U) != 0U;      // p
    const auto select = static_cast<unsigned>((flags >> 4U) & 0x0FU);
    const auto neg = static_cast<unsigned>((flags >> 8U) & 0x0FU);

    // m0 belongs to the full rotation matrix; read here for alignment.
    const auto m0 = read_u16(data, offset + 2U, "object m0");

    std::size_t cursor = offset + 4U;

    std::optional<std::array<float, 3>> translation;
    std::optional<Matrix> rotation;
    std::optional<std::array<float, 3>> scale;

    if (!has_no_trans) {
        const auto x = read_u32(data, cursor, "object trans x");
        const auto y = read_u32(data, cursor + 4U, "object trans y");
        const auto z = read_u32(data, cursor + 8U, "object trans z");
        cursor += 12U;
        translation = std::array<float, 3>{
            fix_1_x_12(static_cast<std::int32_t>(x)),
            fix_1_x_12(static_cast<std::int32_t>(y)),
            fix_1_x_12(static_cast<std::int32_t>(z))};
    }

    if (has_pivot) {
        const auto a = fix_1_x_12(sign_extend(read_u16(data, cursor, "pivot a"), 16U));
        const auto b =
            fix_1_x_12(sign_extend(read_u16(data, cursor + 2U, "pivot b"), 16U));
        cursor += 4U;
        rotation = pivot_matrix(select, neg, a, b);
    } else if (!has_no_rot) {
        std::array<float, 9> m{};
        m[0] = fix_1_x_12(sign_extend(m0, 16U));
        for (std::size_t i = 0U; i < 8U; ++i) {
            m[i + 1U] = fix_1_x_12(
                sign_extend(read_u16(data, cursor + i * 2U, "object rot"), 16U));
        }
        cursor += 16U;
        Matrix rot = matrix_identity();
        // m is column-major 3x3; copy directly.
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 3; ++row) {
                rot[static_cast<std::size_t>(col * 4 + row)] =
                    m[static_cast<std::size_t>(col * 3 + row)];
            }
        }
        rotation = rot;
    }

    if (!has_no_scale) {
        const auto x = read_u32(data, cursor, "object scale x");
        const auto y = read_u32(data, cursor + 4U, "object scale y");
        const auto z = read_u32(data, cursor + 8U, "object scale z");
        cursor += 12U;
        scale = std::array<float, 3>{
            fix_1_x_12(static_cast<std::int32_t>(x)),
            fix_1_x_12(static_cast<std::int32_t>(y)),
            fix_1_x_12(static_cast<std::int32_t>(z))};
    }

    // matrix = Translation * Rotation * Scale
    Matrix matrix = matrix_identity();
    if (scale.has_value()) {
        matrix = matrix_scale((*scale)[0], (*scale)[1], (*scale)[2]);
    }
    if (rotation.has_value()) {
        matrix = matrix_multiply(*rotation, matrix);
    }
    if (translation.has_value()) {
        Matrix t = matrix_identity();
        t[12] = (*translation)[0];
        t[13] = (*translation)[1];
        t[14] = (*translation)[2];
        matrix = matrix_multiply(t, matrix);
    }
    return matrix;
}

std::vector<Matrix> read_objects(
    const ByteVector& mdl0,
    const std::size_t objects_offset) {
    const auto dictionary = parse_dictionary(mdl0, objects_offset, "object");
    std::vector<Matrix> objects;
    objects.reserve(dictionary.entries.size());
    for (const auto& entry : dictionary.entries) {
        const auto relative = static_cast<std::size_t>(
            read_entry_u32(entry, "object"));
        objects.push_back(read_object_matrix(mdl0, objects_offset + relative));
    }
    return objects;
}

// Inverse-bind matrices: num_objects entries, each a 4x3 affine matrix (12
// fixed-point words) followed by a 3x3 we ignore.
std::vector<Matrix> read_inverse_binds(
    const ByteVector& mdl0,
    const std::size_t inv_binds_offset,
    const std::size_t num_objects) {
    constexpr std::size_t element_size = (4U * 3U + 3U * 3U) * 4U;  // 84 bytes
    std::vector<Matrix> inv_binds;
    inv_binds.reserve(num_objects);

    std::size_t cursor = inv_binds_offset;
    for (std::size_t index = 0U; index < num_objects; ++index) {
        if (cursor > mdl0.size() || mdl0.size() - cursor < element_size) {
            break;
        }
        std::array<float, 12> m{};
        for (std::size_t i = 0U; i < 12U; ++i) {
            m[i] = fix_1_x_12(static_cast<std::int32_t>(
                read_u32(mdl0, cursor + i * 4U, "inverse bind")));
        }
        // Stored as three rows of a 4x3 affine matrix (rows 0..2), column 3 is
        // the translation. Build column-major.
        Matrix matrix = matrix_identity();
        matrix[0] = m[0]; matrix[1] = m[1]; matrix[2] = m[2];
        matrix[4] = m[3]; matrix[5] = m[4]; matrix[6] = m[5];
        matrix[8] = m[6]; matrix[9] = m[7]; matrix[10] = m[8];
        matrix[12] = m[9]; matrix[13] = m[10]; matrix[14] = m[11];
        inv_binds.push_back(matrix);
        cursor += element_size;
    }
    return inv_binds;
}

// ----- Materials (only the texture size, for UV normalization) -------------

struct MaterialInfo final {
    float width = 0.0F;
    float height = 0.0F;
};

std::vector<MaterialInfo> read_materials(
    const ByteVector& mdl0,
    const std::size_t materials_offset) {
    // The materials block starts with two u16 pairing offsets; the material
    // dictionary follows.
    const auto dictionary =
        parse_dictionary(mdl0, materials_offset + 4U, "material");
    std::vector<MaterialInfo> materials;
    materials.reserve(dictionary.entries.size());
    for (const auto& entry : dictionary.entries) {
        const auto relative = static_cast<std::size_t>(
            read_entry_u32(entry, "material"));
        const auto material_offset = materials_offset + relative;
        const auto teximage_param =
            read_u32(mdl0, material_offset + 0x14U, "teximage param");
        const auto s_size = (teximage_param >> 20U) & 0x07U;
        const auto t_size = (teximage_param >> 23U) & 0x07U;
        materials.push_back(MaterialInfo{
            static_cast<float>(8U << s_size),
            static_cast<float>(8U << t_size)});
    }
    return materials;
}

// ----- Render command stream (SBC) -----------------------------------------

struct SkinTerm final {
    float weight = 0.0F;
    std::uint8_t stack_pos = 0U;
    std::uint8_t inv_bind_idx = 0U;
};

enum class OpKind {
    LoadMatrix,
    StoreMatrix,
    MulObject,
    Skin,
    ScaleUp,
    ScaleDown,
    BindMaterial,
    Draw,
};

struct RenderOp final {
    OpKind kind;
    std::uint8_t index = 0U;  // stack_pos / object / material / piece
    std::vector<SkinTerm> terms;
};

// Length in bytes of the fixed-size render commands (excluding 0x09).
std::size_t sbc_param_bytes(const std::uint8_t opcode) {
    switch (opcode) {
        case 0x00: case 0x01: case 0x0B: case 0x2B: case 0x40: case 0x80:
            return 0U;
        case 0x03: case 0x04: case 0x05: case 0x07: case 0x08:
        case 0x24: case 0x44:
            return 1U;
        case 0x02: case 0x0C: case 0x0D: case 0x47:
            return 2U;
        case 0x06:
            return 3U;
        case 0x26: case 0x46:
            return 4U;
        case 0x66:
            return 5U;
        default:
            throw std::runtime_error(
                "unknown render command opcode "
                + std::to_string(static_cast<int>(opcode)));
    }
}

std::vector<RenderOp> parse_render_commands(
    const ByteVector& mdl0,
    const std::size_t offset,
    const std::size_t limit) {
    std::vector<RenderOp> ops;
    std::size_t cursor = offset;
    const auto end = offset + limit;

    const auto byte_at = [&](const std::size_t position) -> std::uint8_t {
        if (position >= end) {
            throw std::runtime_error("render command stream is truncated");
        }
        return mdl0[position];
    };

    while (cursor < end) {
        const auto opcode = byte_at(cursor);
        ++cursor;

        if (opcode == 0x01) {
            break;  // end of render commands
        }

        std::size_t params_len = 0U;
        if (opcode == 0x09) {
            const auto count = byte_at(cursor + 1U);
            params_len = 1U + 1U + 3U * static_cast<std::size_t>(count);
        } else {
            params_len = sbc_param_bytes(opcode);
        }

        if (cursor + params_len > end) {
            throw std::runtime_error("render command parameters exceed stream");
        }
        const std::size_t params = cursor;
        cursor += params_len;

        switch (opcode) {
            case 0x00:  // NOP
            case 0x02:  // visibility / unknown
                break;
            case 0x03:
                ops.push_back({OpKind::LoadMatrix, mdl0[params], {}});
                break;
            case 0x04: case 0x24: case 0x44:
                ops.push_back({OpKind::BindMaterial, mdl0[params], {}});
                break;
            case 0x05:
                ops.push_back({OpKind::Draw, mdl0[params], {}});
                break;
            case 0x06: case 0x26: case 0x46: case 0x66: {
                const auto object_idx = mdl0[params];
                std::optional<std::uint8_t> store_pos;
                std::optional<std::uint8_t> load_pos;
                if (opcode == 0x26) {
                    store_pos = mdl0[params + 3U];
                } else if (opcode == 0x46) {
                    load_pos = mdl0[params + 3U];
                } else if (opcode == 0x66) {
                    store_pos = mdl0[params + 3U];
                    load_pos = mdl0[params + 4U];
                }
                if (load_pos.has_value()) {
                    ops.push_back({OpKind::LoadMatrix, *load_pos, {}});
                }
                ops.push_back({OpKind::MulObject, object_idx, {}});
                if (store_pos.has_value()) {
                    ops.push_back({OpKind::StoreMatrix, *store_pos, {}});
                }
                break;
            }
            case 0x09: {
                const auto store_pos = mdl0[params];
                const auto count = mdl0[params + 1U];
                std::vector<SkinTerm> terms;
                terms.reserve(count);
                for (std::size_t i = 0U; i < count; ++i) {
                    const auto base = params + 2U + i * 3U;
                    terms.push_back(SkinTerm{
                        static_cast<float>(mdl0[base + 2U]) / 256.0F,
                        mdl0[base],
                        mdl0[base + 1U]});
                }
                ops.push_back({OpKind::Skin, store_pos, std::move(terms)});
                ops.push_back({OpKind::StoreMatrix, store_pos, {}});
                break;
            }
            case 0x0B:
                ops.push_back({OpKind::ScaleUp, 0U, {}});
                break;
            case 0x2B:
                ops.push_back({OpKind::ScaleDown, 0U, {}});
                break;
            default:
                break;  // billboards and other commands: no geometry effect
        }
    }

    return ops;
}

// ----- GPU command primitive assembly --------------------------------------

std::size_t gpu_parameter_words(const std::uint8_t opcode) {
    switch (opcode) {
        case 0x00: case 0x11: case 0x15: case 0x41:
            return 0U;
        case 0x10: case 0x12: case 0x13: case 0x14: case 0x20: case 0x21:
        case 0x22: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28:
        case 0x29: case 0x2A: case 0x2B: case 0x30: case 0x31: case 0x32:
        case 0x33: case 0x40: case 0x50: case 0x60: case 0x72:
            return 1U;
        case 0x23: case 0x71:
            return 2U;
        case 0x1B: case 0x1C: case 0x70:
            return 3U;
        case 0x1A:
            return 9U;
        case 0x17: case 0x19:
            return 12U;
        case 0x16: case 0x18:
            return 16U;
        case 0x34:
            return 32U;
        default:
            throw std::runtime_error("unsupported Nintendo DS GPU opcode");
    }
}

struct PrimitiveGroup final {
    int mode = -1;
    std::vector<std::uint32_t> vertices;
};

void flush_primitive_group(
    const PrimitiveGroup& group,
    std::vector<std::uint32_t>& indices) {
    const auto& v = group.vertices;
    const auto count = v.size();

    switch (group.mode) {
        case 0:
            for (std::size_t i = 0U; i + 2U < count; i += 3U) {
                indices.insert(indices.end(), {v[i], v[i + 1U], v[i + 2U]});
            }
            break;
        case 1:
            for (std::size_t i = 0U; i + 3U < count; i += 4U) {
                indices.insert(indices.end(), {v[i], v[i + 1U], v[i + 2U]});
                indices.insert(indices.end(), {v[i], v[i + 2U], v[i + 3U]});
            }
            break;
        case 2:
            for (std::size_t i = 2U; i < count; ++i) {
                if ((i & 1U) == 0U) {
                    indices.insert(indices.end(), {v[i - 2U], v[i - 1U], v[i]});
                } else {
                    indices.insert(indices.end(), {v[i - 1U], v[i - 2U], v[i]});
                }
            }
            break;
        case 3:
            for (std::size_t i = 0U; i + 3U < count; i += 2U) {
                indices.insert(indices.end(), {v[i], v[i + 1U], v[i + 3U]});
                indices.insert(indices.end(), {v[i], v[i + 3U], v[i + 2U]});
            }
            break;
        default:
            break;
    }
}

// ----- Model decode: matrix-stack VM + piece drawing -----------------------

struct Builder final {
    const ByteVector& mdl0;
    const std::vector<Matrix>& objects;
    const std::vector<Matrix>& inv_binds;
    const std::vector<MaterialInfo>& materials;
    float up_scale = 1.0F;
    float down_scale = 1.0F;

    // Piece (mesh) command spans and names, indexed by piece id.
    std::vector<std::string> piece_names;
    std::vector<std::size_t> piece_offsets;
    std::vector<std::size_t> piece_lengths;

    // Matrix palette. Index 0 is the identity.
    std::vector<Matrix> palette{matrix_identity()};
    std::uint32_t current_matrix = 0U;
    std::array<int, 32> stack{};  // palette index per stack slot, -1 = unset

    float texture_width = 0.0F;
    float texture_height = 0.0F;

    khdays::assets::NeutralModel& out;

    Builder(
        const ByteVector& mdl0_,
        const std::vector<Matrix>& objects_,
        const std::vector<Matrix>& inv_binds_,
        const std::vector<MaterialInfo>& materials_,
        khdays::assets::NeutralModel& out_)
        : mdl0(mdl0_),
          objects(objects_),
          inv_binds(inv_binds_),
          materials(materials_),
          out(out_) {
        stack.fill(-1);
    }

    std::uint32_t add_matrix(const Matrix& matrix) {
        palette.push_back(matrix);
        return static_cast<std::uint32_t>(palette.size() - 1U);
    }

    std::uint32_t fetch_from_stack(const std::uint8_t stack_pos) {
        if (stack[stack_pos] < 0) {
            stack[stack_pos] = static_cast<int>(current_matrix);
        }
        return static_cast<std::uint32_t>(stack[stack_pos]);
    }

    void run(const std::vector<RenderOp>& ops) {
        for (const auto& op : ops) {
            switch (op.kind) {
                case OpKind::LoadMatrix:
                    current_matrix = fetch_from_stack(op.index);
                    break;
                case OpKind::StoreMatrix:
                    stack[op.index] = static_cast<int>(current_matrix);
                    break;
                case OpKind::MulObject: {
                    if (op.index >= objects.size()) {
                        throw std::runtime_error("object index out of range");
                    }
                    const auto matrix = matrix_multiply(
                        palette[current_matrix], objects[op.index]);
                    current_matrix = add_matrix(matrix);
                    break;
                }
                case OpKind::Skin: {
                    Matrix acc = matrix_zero();
                    for (const auto& term : op.terms) {
                        if (term.inv_bind_idx >= inv_binds.size()) {
                            throw std::runtime_error(
                                "inverse bind index out of range");
                        }
                        const auto slot = fetch_from_stack(term.stack_pos);
                        const auto product = matrix_multiply(
                            palette[slot], inv_binds[term.inv_bind_idx]);
                        matrix_add_scaled(acc, product, term.weight);
                    }
                    current_matrix = add_matrix(acc);
                    break;
                }
                case OpKind::ScaleUp:
                    current_matrix = add_matrix(matrix_multiply(
                        palette[current_matrix],
                        matrix_scale(up_scale, up_scale, up_scale)));
                    break;
                case OpKind::ScaleDown:
                    current_matrix = add_matrix(matrix_multiply(
                        palette[current_matrix],
                        matrix_scale(down_scale, down_scale, down_scale)));
                    break;
                case OpKind::BindMaterial:
                    if (op.index < materials.size()) {
                        texture_width = materials[op.index].width;
                        texture_height = materials[op.index].height;
                    }
                    break;
                case OpKind::Draw:
                    draw_piece(op.index);
                    break;
            }
        }
    }

    void draw_piece(const std::uint8_t piece_idx) {
        if (piece_idx >= piece_names.size()) {
            throw std::runtime_error("piece index out of range");
        }

        khdays::assets::NeutralMesh mesh;
        mesh.name = piece_names[piece_idx];

        std::array<float, 3> position{0.0F, 0.0F, 0.0F};
        std::array<float, 2> texel{0.0F, 0.0F};
        std::array<std::uint8_t, 4> color{255U, 255U, 255U, 255U};
        PrimitiveGroup group;

        const auto emit_vertex = [&]() {
            khdays::assets::NeutralVertex vertex;
            vertex.position = position;
            vertex.texcoord = {
                texture_width > 0.0F ? texel[0] / texture_width : texel[0],
                texture_height > 0.0F ? texel[1] / texture_height : texel[1]};
            vertex.color = color;
            vertex.matrix_index = current_matrix;
            const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(vertex);
            group.vertices.push_back(index);
        };

        const auto command_offset = piece_offsets[piece_idx];
        const auto command_length = piece_lengths[piece_idx];
        std::size_t cursor = command_offset;
        const auto end = command_offset + command_length;

        while (cursor < end) {
            if (end - cursor < 4U) {
                throw std::runtime_error("mesh GPU command packet is truncated");
            }
            const auto opcode_word = read_u32(mdl0, cursor, "GPU opcode packet");
            cursor += 4U;

            const std::array<std::uint8_t, 4> opcodes{
                static_cast<std::uint8_t>(opcode_word & 0xFFU),
                static_cast<std::uint8_t>((opcode_word >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((opcode_word >> 16U) & 0xFFU),
                static_cast<std::uint8_t>((opcode_word >> 24U) & 0xFFU)};

            for (const auto opcode : opcodes) {
                const auto words = gpu_parameter_words(opcode);
                if (words * 4U > end - cursor) {
                    throw std::runtime_error(
                        "GPU parameters exceed mesh command list");
                }
                const auto param = [&](const std::size_t which) {
                    return read_u32(
                        mdl0, cursor + which * 4U, "GPU command parameter");
                };

                switch (opcode) {
                    case 0x14:  // MTX_RESTORE
                        current_matrix = fetch_from_stack(
                            static_cast<std::uint8_t>(param(0U) & 0x1FU));
                        break;
                    case 0x20: {  // COLOR
                        const auto value = param(0U);
                        const auto expand = [](std::uint32_t c) {
                            c &= 0x1FU;
                            return static_cast<std::uint8_t>((c << 3U) | (c >> 2U));
                        };
                        color = {
                            expand(value), expand(value >> 5U),
                            expand(value >> 10U), 255U};
                        break;
                    }
                    case 0x22:  // TEXCOORD (1.11.4 texels)
                        texel[0] = static_cast<float>(
                            sign_extend(param(0U) & 0xFFFFU, 16U)) / 16.0F;
                        texel[1] = static_cast<float>(
                            sign_extend((param(0U) >> 16U) & 0xFFFFU, 16U)) / 16.0F;
                        break;
                    case 0x2A: {  // TEXIMAGE_PARAM (per-piece override)
                        const auto value = param(0U);
                        texture_width =
                            static_cast<float>(8U << ((value >> 20U) & 0x07U));
                        texture_height =
                            static_cast<float>(8U << ((value >> 23U) & 0x07U));
                        break;
                    }
                    case 0x23: {  // VTX_16
                        const auto p0 = param(0U);
                        const auto p1 = param(1U);
                        position[0] = fix_1_x_12(sign_extend(p0 & 0xFFFFU, 16U));
                        position[1] =
                            fix_1_x_12(sign_extend((p0 >> 16U) & 0xFFFFU, 16U));
                        position[2] = fix_1_x_12(sign_extend(p1 & 0xFFFFU, 16U));
                        emit_vertex();
                        break;
                    }
                    case 0x24: {  // VTX_10 (4.6 signed)
                        const auto value = param(0U);
                        position[0] =
                            static_cast<float>(sign_extend(value, 10U)) / 64.0F;
                        position[1] =
                            static_cast<float>(sign_extend(value >> 10U, 10U)) / 64.0F;
                        position[2] =
                            static_cast<float>(sign_extend(value >> 20U, 10U)) / 64.0F;
                        emit_vertex();
                        break;
                    }
                    case 0x25: {  // VTX_XY
                        const auto value = param(0U);
                        position[0] = fix_1_x_12(sign_extend(value & 0xFFFFU, 16U));
                        position[1] =
                            fix_1_x_12(sign_extend((value >> 16U) & 0xFFFFU, 16U));
                        emit_vertex();
                        break;
                    }
                    case 0x26: {  // VTX_XZ
                        const auto value = param(0U);
                        position[0] = fix_1_x_12(sign_extend(value & 0xFFFFU, 16U));
                        position[2] =
                            fix_1_x_12(sign_extend((value >> 16U) & 0xFFFFU, 16U));
                        emit_vertex();
                        break;
                    }
                    case 0x27: {  // VTX_YZ
                        const auto value = param(0U);
                        position[1] = fix_1_x_12(sign_extend(value & 0xFFFFU, 16U));
                        position[2] =
                            fix_1_x_12(sign_extend((value >> 16U) & 0xFFFFU, 16U));
                        emit_vertex();
                        break;
                    }
                    case 0x28: {  // VTX_DIFF (10-bit signed 4.12 deltas)
                        const auto value = param(0U);
                        position[0] +=
                            static_cast<float>(sign_extend(value, 10U)) / 4096.0F;
                        position[1] +=
                            static_cast<float>(sign_extend(value >> 10U, 10U)) / 4096.0F;
                        position[2] +=
                            static_cast<float>(sign_extend(value >> 20U, 10U)) / 4096.0F;
                        emit_vertex();
                        break;
                    }
                    case 0x40:  // BEGIN_VTXS
                        flush_primitive_group(group, mesh.indices);
                        group = PrimitiveGroup{};
                        group.mode = static_cast<int>(param(0U) & 0x03U);
                        break;
                    case 0x41:  // END_VTXS
                        flush_primitive_group(group, mesh.indices);
                        group = PrimitiveGroup{};
                        break;
                    default:
                        break;
                }

                cursor += words * 4U;
            }
        }

        flush_primitive_group(group, mesh.indices);
        out.meshes.push_back(std::move(mesh));
    }
};

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
            "model index " + std::to_string(model_index) + " is out of range ("
            + std::to_string(model_dictionary.names.size()) + " models)");
    }

    const auto model_offset = static_cast<std::size_t>(
        read_entry_u32(model_dictionary.entries[model_index], "model"));
    if (model_offset > mdl0.size() || mdl0.size() - model_offset < 0x40U) {
        throw std::runtime_error("model header exceeds MDL0");
    }

    const auto render_off = static_cast<std::size_t>(
        read_u32(mdl0, model_offset + 0x04U, "render commands offset"));
    const auto materials_off = static_cast<std::size_t>(
        read_u32(mdl0, model_offset + 0x08U, "materials offset"));
    const auto pieces_off = static_cast<std::size_t>(
        read_u32(mdl0, model_offset + 0x0CU, "pieces offset"));
    const auto inv_binds_off = static_cast<std::size_t>(
        read_u32(mdl0, model_offset + 0x10U, "inverse binds offset"));
    const auto num_objects = static_cast<std::size_t>(mdl0[model_offset + 0x17U]);

    NeutralModel result;
    result.name = model_dictionary.names[model_index];
    result.header_vertex_count = read_u16(mdl0, model_offset + 0x24U, "vertex count");

    const auto objects = read_objects(mdl0, model_offset + 0x40U);
    const auto inv_binds =
        read_inverse_binds(mdl0, model_offset + inv_binds_off, num_objects);
    const auto materials = read_materials(mdl0, model_offset + materials_off);

    Builder builder{mdl0, objects, inv_binds, materials, result};
    builder.up_scale = fix_1_x_12(static_cast<std::int32_t>(
        read_u32(mdl0, model_offset + 0x1CU, "up scale")));
    builder.down_scale = fix_1_x_12(static_cast<std::int32_t>(
        read_u32(mdl0, model_offset + 0x20U, "down scale")));

    // Collect the piece (mesh) command spans.
    const auto mesh_list_offset = model_offset + pieces_off;
    const auto mesh_dictionary =
        parse_dictionary(mdl0, mesh_list_offset, "mesh");
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
        if (command_offset > mdl0.size()
            || command_length > mdl0.size() - command_offset) {
            throw std::runtime_error("mesh GPU command list exceeds MDL0");
        }
        builder.piece_names.push_back(mesh_dictionary.names[index]);
        builder.piece_offsets.push_back(command_offset);
        builder.piece_lengths.push_back(command_length);
    }

    const auto render_ops =
        parse_render_commands(mdl0, model_offset + render_off, materials_off - render_off);
    builder.run(render_ops);

    result.palette = builder.palette;
    return result;
}

std::array<float, 3> transform_point(
    const std::array<float, 16>& matrix,
    const std::array<float, 3>& point) {
    return matrix_transform_point(matrix, point);
}

std::array<float, 3> posed_position(
    const NeutralModel& model,
    const NeutralVertex& vertex) {
    if (vertex.matrix_index >= model.palette.size()) {
        return vertex.position;
    }
    return matrix_transform_point(model.palette[vertex.matrix_index], vertex.position);
}

std::string to_wavefront_obj(const NeutralModel& model) {
    std::ostringstream stream;
    stream << "# khdays-port neutral mesh export\n";
    stream << "# model: " << model.name << '\n';

    std::uint32_t vertex_base = 1U;  // OBJ indices are 1-based

    for (const auto& mesh : model.meshes) {
        stream << "o " << mesh.name << '\n';

        for (const auto& vertex : mesh.vertices) {
            const auto p = posed_position(model, vertex);
            stream << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
        }
        for (const auto& vertex : mesh.vertices) {
            stream << "vt " << vertex.texcoord[0] << ' '
                   << vertex.texcoord[1] << '\n';
        }

        for (std::size_t i = 0U; i + 2U < mesh.indices.size(); i += 3U) {
            const auto a = vertex_base + mesh.indices[i];
            const auto b = vertex_base + mesh.indices[i + 1U];
            const auto c = vertex_base + mesh.indices[i + 2U];
            stream << "f " << a << '/' << a << ' ' << b << '/' << b
                   << ' ' << c << '/' << c << '\n';
        }

        vertex_base += static_cast<std::uint32_t>(mesh.vertices.size());
    }

    return stream.str();
}

}  // namespace khdays::assets
