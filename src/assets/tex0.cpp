#include "khdays/assets/tex0.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
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

struct Palette final {
    std::string name;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::vector<std::array<std::uint8_t, 4>> colors;
};

struct TextureParameter final {
    std::uint32_t offset_units = 0;
    int size_s_exponent = 0;
    int size_t_exponent = 0;
    int format_code = 0;
    bool color_zero_transparent = false;
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
        throw std::runtime_error(
            "cannot open resource file: " + path.string());
    }

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error(
            "cannot determine resource size: " + path.string());
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
            "cannot read complete resource file: " + path.string());
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
    if (data.size() < 0x10U) {
        throw std::runtime_error("resource is smaller than a Nitro header");
    }

    if (!has_magic(data, 0U, "BMD0") && !has_magic(data, 0U, "BTX0")) {
        throw std::runtime_error("resource is not BMD0 or BTX0");
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
    const SectionView& view) {
    return ByteVector{
        data.begin() + static_cast<std::ptrdiff_t>(view.offset),
        data.begin() + static_cast<std::ptrdiff_t>(view.offset + view.size),
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
            std::string{label} + " dictionary exceeds TEX0");
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
            std::string{label} + " dictionary entries exceed dictionary");
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

TextureParameter decode_parameter(const std::uint32_t value) {
    TextureParameter parameter;
    parameter.offset_units = value & 0xFFFFU;
    parameter.size_s_exponent = static_cast<int>((value >> 20U) & 0x07U);
    parameter.size_t_exponent = static_cast<int>((value >> 23U) & 0x07U);
    parameter.format_code = static_cast<int>((value >> 26U) & 0x07U);
    parameter.color_zero_transparent = ((value >> 29U) & 0x01U) != 0U;
    return parameter;
}

std::string format_name(const int format_code) {
    switch (format_code) {
        case 1:
            return "A3I5";
        case 2:
            return "PLTT4";
        case 3:
            return "PLTT16";
        case 4:
            return "PLTT256";
        case 5:
            return "COMP4X4";
        case 6:
            return "A5I3";
        case 7:
            return "DIRECT";
        default:
            return "UNKNOWN";
    }
}

std::size_t texture_data_size(
    const int format_code,
    const int width,
    const int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("texture dimensions are invalid");
    }

    const auto pixels =
        static_cast<std::size_t>(width)
        * static_cast<std::size_t>(height);

    switch (format_code) {
        case 1:
        case 4:
        case 6:
            return pixels;
        case 2:
            return (pixels + 3U) / 4U;
        case 3:
            return (pixels + 1U) / 2U;
        case 5:
            throw std::runtime_error(
                "Nintendo DS 4x4 compressed textures are not supported yet");
        case 7:
            return pixels * 2U;
        default:
            throw std::runtime_error("unsupported TEX0 texture format");
    }
}

std::array<std::uint8_t, 4> bgr555_to_rgba(
    const std::uint16_t value,
    const std::uint8_t alpha = 255U) {
    const auto red5 = static_cast<std::uint8_t>(value & 0x1FU);
    const auto green5 =
        static_cast<std::uint8_t>((value >> 5U) & 0x1FU);
    const auto blue5 =
        static_cast<std::uint8_t>((value >> 10U) & 0x1FU);

    return {
        static_cast<std::uint8_t>((red5 << 3U) | (red5 >> 2U)),
        static_cast<std::uint8_t>((green5 << 3U) | (green5 >> 2U)),
        static_cast<std::uint8_t>((blue5 << 3U) | (blue5 >> 2U)),
        alpha,
    };
}

std::vector<Palette> parse_palettes(
    const ByteVector& tex0,
    const Dictionary& dictionary,
    const std::size_t palette_data_offset,
    const std::size_t palette_data_size) {
    if (dictionary.names.size() != dictionary.entries.size()) {
        throw std::runtime_error("palette dictionary is inconsistent");
    }

    std::vector<std::size_t> offsets;
    offsets.reserve(dictionary.entries.size());

    for (std::size_t index = 0U; index < dictionary.entries.size(); ++index) {
        const auto& entry = dictionary.entries[index];
        if (entry.size() < 2U) {
            throw std::runtime_error("palette dictionary entry is too small");
        }

        const std::uint16_t units =
            static_cast<std::uint16_t>(entry[0])
            | (static_cast<std::uint16_t>(entry[1]) << 8U);
        offsets.push_back(static_cast<std::size_t>(units) * 8U);
    }

    std::vector<Palette> palettes;
    palettes.reserve(offsets.size());

    for (std::size_t index = 0U; index < offsets.size(); ++index) {
        const auto start = offsets[index];
        auto end = palette_data_size;

        for (const auto candidate : offsets) {
            if (candidate > start && candidate < end) {
                end = candidate;
            }
        }

        if (start > end || end > palette_data_size) {
            throw std::runtime_error("palette range is invalid");
        }

        const auto absolute_start = palette_data_offset + start;
        const auto absolute_end = palette_data_offset + end;

        if (
            absolute_start > tex0.size()
            || absolute_end > tex0.size()
            || absolute_end < absolute_start) {
            throw std::runtime_error("palette data exceeds TEX0");
        }

        Palette palette;
        palette.name = dictionary.names[index];
        palette.offset = start;
        palette.size = end - start;
        palette.colors.reserve(palette.size / 2U);

        for (
            std::size_t position = absolute_start;
            position + 1U < absolute_end;
            position += 2U) {
            palette.colors.push_back(
                bgr555_to_rgba(read_u16(tex0, position, "palette color")));
        }

        palettes.push_back(std::move(palette));
    }

    return palettes;
}

std::optional<std::size_t> choose_palette(
    const std::string& texture_name,
    const std::size_t texture_index,
    const std::vector<Palette>& palettes) {
    if (palettes.empty()) {
        return std::nullopt;
    }

    const auto expected = texture_name + "_pl";

    for (std::size_t index = 0U; index < palettes.size(); ++index) {
        if (palettes[index].name == expected) {
            return index;
        }
    }

    for (std::size_t index = 0U; index < palettes.size(); ++index) {
        if (palettes[index].name == texture_name) {
            return index;
        }
    }

    if (texture_index < palettes.size()) {
        return texture_index;
    }

    return 0U;
}

std::vector<std::uint8_t> decode_rgba(
    const ByteVector& raw,
    const int width,
    const int height,
    const int format_code,
    const Palette* palette,
    const bool color_zero_transparent) {
    const auto pixel_count =
        static_cast<std::size_t>(width)
        * static_cast<std::size_t>(height);

    std::vector<std::uint8_t> rgba;
    rgba.reserve(pixel_count * 4U);

    if (format_code == 7) {
        if (raw.size() < pixel_count * 2U) {
            throw std::runtime_error("direct-color texture is truncated");
        }

        for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
            const auto value = read_u16(
                raw,
                pixel * 2U,
                "direct-color pixel");
            const auto color = bgr555_to_rgba(
                value,
                (value & 0x8000U) != 0U ? 255U : 0U);
            rgba.insert(rgba.end(), color.begin(), color.end());
        }

        return rgba;
    }

    if (palette == nullptr) {
        throw std::runtime_error("indexed texture has no palette");
    }

    std::vector<std::uint16_t> indices;
    std::vector<std::uint8_t> alphas;
    indices.reserve(pixel_count);
    alphas.reserve(pixel_count);

    switch (format_code) {
        case 1:
            for (const auto value : raw) {
                indices.push_back(value & 0x1FU);
                alphas.push_back(
                    static_cast<std::uint8_t>(
                        ((value >> 5U) & 0x07U) * 255U / 7U));
            }
            break;

        case 2:
            for (const auto value : raw) {
                for (const int shift : {0, 2, 4, 6}) {
                    indices.push_back(
                        static_cast<std::uint16_t>(
                            (value >> shift) & 0x03U));
                    alphas.push_back(255U);
                }
            }
            break;

        case 3:
            for (const auto value : raw) {
                indices.push_back(value & 0x0FU);
                alphas.push_back(255U);
                indices.push_back((value >> 4U) & 0x0FU);
                alphas.push_back(255U);
            }
            break;

        case 4:
            for (const auto value : raw) {
                indices.push_back(value);
                alphas.push_back(255U);
            }
            break;

        case 6:
            for (const auto value : raw) {
                indices.push_back(value & 0x07U);
                alphas.push_back(
                    static_cast<std::uint8_t>(
                        ((value >> 3U) & 0x1FU) * 255U / 31U));
            }
            break;

        default:
            throw std::runtime_error(
                "texture format is not an indexed format");
    }

    if (indices.size() < pixel_count || alphas.size() < pixel_count) {
        throw std::runtime_error("decoded texture contains too few pixels");
    }

    for (std::size_t pixel = 0U; pixel < pixel_count; ++pixel) {
        const auto index = static_cast<std::size_t>(indices[pixel]);

        std::array<std::uint8_t, 4> color{255U, 0U, 255U, 255U};
        if (index < palette->colors.size()) {
            color = palette->colors[index];
        }

        color[3] = alphas[pixel];
        if (color_zero_transparent && index == 0U) {
            color[3] = 0U;
        }

        rgba.insert(rgba.end(), color.begin(), color.end());
    }

    return rgba;
}

struct Tex0Data final {
    ByteVector section;
    Dictionary texture_dictionary;
    Dictionary palette_dictionary;
    std::size_t texture_data_offset = 0;
    std::size_t texture_data_size = 0;
    std::size_t palette_data_offset = 0;
    std::size_t palette_data_size = 0;
    std::vector<Palette> palettes;
};

Tex0Data load_tex0(const ByteVector& file) {
    const auto sections = parse_sections(file);
    const auto iterator = sections.find("TEX0");

    if (iterator == sections.end()) {
        throw std::runtime_error("resource contains no TEX0 section");
    }

    Tex0Data result;
    result.section = copy_section(file, iterator->second);

    if (result.section.size() < 0x3CU) {
        throw std::runtime_error("TEX0 section is too small");
    }

    result.texture_data_size =
        static_cast<std::size_t>(
            read_u16(result.section, 0x0CU, "texture data size"))
        * 8U;
    const auto texture_dictionary_offset =
        static_cast<std::size_t>(
            read_u16(
                result.section,
                0x0EU,
                "texture dictionary offset"));
    result.texture_data_offset =
        static_cast<std::size_t>(
            read_u32(
                result.section,
                0x14U,
                "texture data offset"));

    result.palette_data_size =
        static_cast<std::size_t>(
            read_u16(result.section, 0x30U, "palette data size"))
        * 8U;
    const auto palette_dictionary_offset =
        static_cast<std::size_t>(
            read_u32(
                result.section,
                0x34U,
                "palette dictionary offset"));
    result.palette_data_offset =
        static_cast<std::size_t>(
            read_u32(
                result.section,
                0x38U,
                "palette data offset"));

    if (
        result.texture_data_offset > result.section.size()
        || result.texture_data_size
            > result.section.size() - result.texture_data_offset) {
        throw std::runtime_error("texture data exceeds TEX0");
    }

    if (
        result.palette_data_offset > result.section.size()
        || result.palette_data_size
            > result.section.size() - result.palette_data_offset) {
        throw std::runtime_error("palette data exceeds TEX0");
    }

    result.texture_dictionary = parse_dictionary(
        result.section,
        texture_dictionary_offset,
        "texture");
    result.palette_dictionary = parse_dictionary(
        result.section,
        palette_dictionary_offset,
        "palette");
    result.palettes = parse_palettes(
        result.section,
        result.palette_dictionary,
        result.palette_data_offset,
        result.palette_data_size);

    return result;
}

}  // namespace

namespace khdays::assets {

namespace {

DecodedTexture load_tex0_texture_from(
    const ByteVector& file,
    const std::optional<std::string_view> requested_name) {
    const auto tex0 = load_tex0(file);

    if (
        tex0.texture_dictionary.names.size()
        != tex0.texture_dictionary.entries.size()) {
        throw std::runtime_error("texture dictionary is inconsistent");
    }

    if (tex0.texture_dictionary.names.empty()) {
        throw std::runtime_error("TEX0 contains no textures");
    }

    std::size_t texture_index = 0U;

    if (requested_name.has_value()) {
        const auto iterator = std::find(
            tex0.texture_dictionary.names.begin(),
            tex0.texture_dictionary.names.end(),
            *requested_name);

        if (iterator == tex0.texture_dictionary.names.end()) {
            std::string available;
            for (const auto& name : tex0.texture_dictionary.names) {
                if (!available.empty()) {
                    available += ", ";
                }
                available += name;
            }

            throw std::runtime_error(
                "texture '" + std::string{*requested_name}
                + "' was not found; available: " + available);
        }

        texture_index = static_cast<std::size_t>(
            std::distance(
                tex0.texture_dictionary.names.begin(),
                iterator));
    }

    const auto& texture_name =
        tex0.texture_dictionary.names[texture_index];
    const auto& entry =
        tex0.texture_dictionary.entries[texture_index];

    if (entry.size() < 4U) {
        throw std::runtime_error("texture dictionary entry is too small");
    }

    const std::uint32_t parameter_value =
        static_cast<std::uint32_t>(entry[0])
        | (static_cast<std::uint32_t>(entry[1]) << 8U)
        | (static_cast<std::uint32_t>(entry[2]) << 16U)
        | (static_cast<std::uint32_t>(entry[3]) << 24U);

    const auto parameter = decode_parameter(parameter_value);
    const auto width = 8 << parameter.size_s_exponent;
    const auto height = 8 << parameter.size_t_exponent;
    const auto data_size =
        texture_data_size(parameter.format_code, width, height);
    const auto relative_data_offset =
        static_cast<std::size_t>(parameter.offset_units) * 8U;
    const auto absolute_data_offset =
        tex0.texture_data_offset + relative_data_offset;

    if (
        relative_data_offset > tex0.texture_data_size
        || data_size > tex0.texture_data_size - relative_data_offset
        || absolute_data_offset > tex0.section.size()
        || data_size > tex0.section.size() - absolute_data_offset) {
        throw std::runtime_error("texture pixel data exceeds TEX0");
    }

    const ByteVector raw{
        tex0.section.begin()
            + static_cast<std::ptrdiff_t>(absolute_data_offset),
        tex0.section.begin()
            + static_cast<std::ptrdiff_t>(absolute_data_offset + data_size),
    };

    const Palette* palette = nullptr;
    std::optional<std::string> palette_name;

    if (parameter.format_code != 7) {
        const auto palette_index = choose_palette(
            texture_name,
            texture_index,
            tex0.palettes);

        if (!palette_index.has_value()) {
            throw std::runtime_error("texture has no matching palette");
        }

        palette = &tex0.palettes[*palette_index];
        palette_name = palette->name;
    }

    DecodedTexture result;
    result.name = texture_name;
    result.palette_name = palette_name;
    result.format_name = format_name(parameter.format_code);
    result.width = width;
    result.height = height;
    result.color_zero_transparent =
        parameter.color_zero_transparent;
    result.rgba = decode_rgba(
        raw,
        width,
        height,
        parameter.format_code,
        palette,
        parameter.color_zero_transparent);

    return result;
}

}  // namespace

std::vector<std::string> list_tex0_textures(
    const std::filesystem::path& input_path) {
    return load_tex0(read_file(input_path)).texture_dictionary.names;
}

std::vector<std::string> list_tex0_textures(
    const std::uint8_t* data, const std::size_t size) {
    return load_tex0(ByteVector{data, data + size}).texture_dictionary.names;
}

DecodedTexture load_tex0_texture(
    const std::filesystem::path& input_path,
    const std::optional<std::string_view> requested_name) {
    return load_tex0_texture_from(read_file(input_path), requested_name);
}

DecodedTexture load_tex0_texture(
    const std::uint8_t* data, const std::size_t size,
    const std::optional<std::string_view> requested_name) {
    return load_tex0_texture_from(ByteVector{data, data + size}, requested_name);
}

}  // namespace khdays::assets
