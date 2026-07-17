#include "khdays/assets/bmp.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t o) {
    if (o + 2U > d.size()) {
        throw std::runtime_error("BMP: read past end");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t o) {
    if (o + 4U > d.size()) {
        throw std::runtime_error("BMP: read past end");
    }
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open BMP file: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot size BMP file: " + path.string());
    }
    ByteVector data(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    return data;
}

}  // namespace

namespace khdays::assets {

DecodedTexture load_bmp(const std::filesystem::path& input_path) {
    const auto data = read_file(input_path);
    if (data.size() < 0x36U || data[0] != 'B' || data[1] != 'M') {
        throw std::runtime_error("resource is not a BMP file");
    }

    const auto pixel_offset = static_cast<std::size_t>(read_u32(data, 0x0AU));
    const auto width = static_cast<std::int32_t>(read_u32(data, 0x12U));
    const auto height_signed = static_cast<std::int32_t>(read_u32(data, 0x16U));
    const auto bpp = read_u16(data, 0x1CU);
    const auto compression = read_u32(data, 0x1EU);

    if (width <= 0 || height_signed == 0) {
        throw std::runtime_error("BMP has invalid dimensions");
    }
    if (bpp != 24U && bpp != 32U) {
        throw std::runtime_error("only 24- or 32-bit BMP is supported");
    }

    // BI_RGB (0) or BI_BITFIELDS (3). The latter is what to_bmp() writes and
    // what an image editor produces when it saves a 32-bit BMP with alpha: the
    // channel masks are what make that alpha part of the file's meaning rather
    // than a byte readers may ignore. Everything below reads pixels as B,G,R,A,
    // so masks describing any other layout are refused instead of silently
    // decoded into the wrong channels.
    constexpr std::uint32_t kBiRgb = 0U;
    constexpr std::uint32_t kBiBitfields = 3U;
    if (compression != kBiRgb && compression != kBiBitfields) {
        throw std::runtime_error("only uncompressed BMP is supported");
    }
    if (compression == kBiBitfields) {
        if (bpp != 32U) {
            throw std::runtime_error(
                "BI_BITFIELDS is only supported for 32-bit BMP");
        }
        // Masks sit right after the 40-byte core of the info header, whether
        // the header is a BITMAPINFOHEADER with masks appended or a V4/V5.
        if (data.size() < 0x42U) {
            throw std::runtime_error("BMP is too small for its channel masks");
        }
        const auto red = read_u32(data, 0x36U);
        const auto green = read_u32(data, 0x3AU);
        const auto blue = read_u32(data, 0x3EU);
        const auto header_size = read_u32(data, 0x0EU);
        // A plain BITMAPINFOHEADER carries no alpha mask; only V4 and up do.
        const auto alpha =
            (header_size >= 108U && data.size() >= 0x46U) ? read_u32(data, 0x42U) : 0U;
        if (red != 0x00FF0000U || green != 0x0000FF00U || blue != 0x000000FFU
            || (alpha != 0xFF000000U && alpha != 0U)) {
            throw std::runtime_error(
                "BMP channel masks are not the supported BGRA layout");
        }
    }

    const bool bottom_up = height_signed > 0;
    const auto height = static_cast<std::int32_t>(
        bottom_up ? height_signed : -height_signed);
    const auto bytes_per_pixel = static_cast<std::size_t>(bpp / 8U);
    // Rows are padded to a multiple of 4 bytes.
    const auto row_stride =
        ((static_cast<std::size_t>(width) * bytes_per_pixel) + 3U) & ~std::size_t{3U};

    if (pixel_offset + row_stride * static_cast<std::size_t>(height) > data.size()) {
        throw std::runtime_error("BMP pixel data exceeds the file");
    }

    DecodedTexture result;
    result.name = input_path.stem().string();
    result.format_name = "BMP";
    result.width = width;
    result.height = height;
    result.rgba.resize(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);

    for (std::int32_t y = 0; y < height; ++y) {
        // BMP stores bottom-up by default; emit top-down RGBA.
        const auto src_row = bottom_up ? (height - 1 - y) : y;
        const auto row = pixel_offset
            + static_cast<std::size_t>(src_row) * row_stride;
        for (std::int32_t x = 0; x < width; ++x) {
            const auto src = row + static_cast<std::size_t>(x) * bytes_per_pixel;
            const auto dst =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                 + static_cast<std::size_t>(x)) * 4U;
            result.rgba[dst + 0U] = data[src + 2U];  // R (BMP is BGR)
            result.rgba[dst + 1U] = data[src + 1U];  // G
            result.rgba[dst + 2U] = data[src + 0U];  // B
            result.rgba[dst + 3U] =
                bytes_per_pixel == 4U ? data[src + 3U] : 255U;  // A
        }
    }

    return result;
}

}  // namespace khdays::assets
