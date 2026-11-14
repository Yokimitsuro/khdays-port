#include "khdays/assets/dds.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

// DDS as published: "DDS " then a 124-byte header (height at 12, width at 16,
// the pixel format at 76: its four-character code at 84, bit count at 88, the
// red/green/blue/alpha masks at 92), the first picture at 128. BC3 stores 4x4
// blocks of 16 bytes: alpha (two endpoints, sixteen 3-bit indices) then colour
// (two RGB565 endpoints, sixteen 2-bit indices, always the four-colour mode).

namespace khdays::assets {

namespace {

constexpr std::size_t kHeaderSize = 128;

std::uint32_t read_u32(const std::uint8_t* d, const std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1U]) << 8U) |
           (static_cast<std::uint32_t>(d[o + 2U]) << 16U) | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

std::array<std::uint8_t, 3> rgb565(const std::uint32_t c) {
    return {static_cast<std::uint8_t>(((c >> 11U) & 31U) * 255U / 31U),
            static_cast<std::uint8_t>(((c >> 5U) & 63U) * 255U / 63U),
            static_cast<std::uint8_t>((c & 31U) * 255U / 31U)};
}

void decode_bc3(const std::uint8_t* body, const int width, const int height, std::vector<std::uint8_t>& rgba) {
    const int blocks_x = (width + 3) / 4;
    const int blocks_y = (height + 3) / 4;
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            const std::uint8_t* b = body + (static_cast<std::size_t>(by) * blocks_x + bx) * 16U;
            const unsigned a0 = b[0];
            const unsigned a1 = b[1];
            std::array<unsigned, 8> alpha{a0, a1, 0, 0, 0, 0, 0, 0};
            if (a0 > a1) {
                for (unsigned i = 1; i < 7; ++i) {
                    alpha[1 + i] = ((7 - i) * a0 + i * a1) / 7;
                }
            } else {
                for (unsigned i = 1; i < 5; ++i) {
                    alpha[1 + i] = ((5 - i) * a0 + i * a1) / 5;
                }
                alpha[6] = 0;
                alpha[7] = 255;
            }
            std::uint64_t alpha_bits = 0;
            for (int i = 0; i < 6; ++i) {
                alpha_bits |= static_cast<std::uint64_t>(b[2 + i]) << (8 * i);
            }
            const auto c0 = rgb565(static_cast<std::uint32_t>(b[8] | (b[9] << 8)));
            const auto c1 = rgb565(static_cast<std::uint32_t>(b[10] | (b[11] << 8)));
            std::array<std::array<std::uint8_t, 3>, 4> colors{c0, c1, {}, {}};
            for (int c = 0; c < 3; ++c) {
                colors[2][c] = static_cast<std::uint8_t>((2 * c0[c] + c1[c]) / 3);
                colors[3][c] = static_cast<std::uint8_t>((c0[c] + 2 * c1[c]) / 3);
            }
            const std::uint32_t color_bits = read_u32(b, 12);
            for (int k = 0; k < 16; ++k) {
                const int x = bx * 4 + (k & 3);
                const int y = by * 4 + (k >> 2);
                if (x >= width || y >= height) {
                    continue;
                }
                const auto& color = colors[(color_bits >> (2 * k)) & 3U];
                std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * width + x) * 4U];
                px[0] = color[0];
                px[1] = color[1];
                px[2] = color[2];
                px[3] = static_cast<std::uint8_t>(alpha[(alpha_bits >> (3 * k)) & 7U]);
            }
        }
    }
}

}  // namespace

DecodedTexture decode_dds(const std::uint8_t* data, const std::size_t size, const std::string& name) {
    if (size < kHeaderSize || data[0] != 'D' || data[1] != 'D' || data[2] != 'S' || data[3] != ' ') {
        throw std::runtime_error("not a DDS picture: " + name);
    }
    const std::uint32_t height = read_u32(data, 12);
    const std::uint32_t width = read_u32(data, 16);
    if (width == 0 || height == 0 || width > 16384 || height > 16384) {
        throw std::runtime_error("DDS picture of an unusable size: " + name);
    }
    DecodedTexture texture;
    texture.name = name;
    texture.width = static_cast<int>(width);
    texture.height = static_cast<int>(height);
    texture.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 0);
    const std::uint32_t fourcc = read_u32(data, 84);
    const std::uint8_t* body = data + kHeaderSize;
    const std::size_t body_size = size - kHeaderSize;
    if (fourcc == 0x35545844U) {  // "DXT5"
        const std::size_t need = static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) * 16U;
        if (body_size < need) {
            throw std::runtime_error("DDS picture cut short: " + name);
        }
        texture.format_name = "DXT5";
        decode_bc3(body, texture.width, texture.height, texture.rgba);
        return texture;
    }
    if (fourcc == 0 && read_u32(data, 88) == 32) {
        if (body_size < static_cast<std::size_t>(width) * height * 4U) {
            throw std::runtime_error("DDS picture cut short: " + name);
        }
        const std::uint32_t masks[4] = {read_u32(data, 92), read_u32(data, 96), read_u32(data, 100),
                                        read_u32(data, 104)};
        texture.format_name = "RGBA32";
        // each mask's shift and width found once, not per texel; 8-bit masks need no widening
        unsigned shifts[4] = {0, 0, 0, 0};
        std::uint32_t maxes[4] = {0, 0, 0, 0};
        bool bytes_only = true;
        for (int c = 0; c < 4; ++c) {
            if (masks[c] == 0) {
                bytes_only = false;
                continue;
            }
            while (((masks[c] >> shifts[c]) & 1U) == 0) {
                ++shifts[c];
            }
            maxes[c] = masks[c] >> shifts[c];
            bytes_only = bytes_only && maxes[c] == 255U;
        }
        std::uint8_t* out = texture.rgba.data();
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            const std::uint32_t pixel = read_u32(body, i * 4U);
            for (int c = 0; c < 4; ++c) {
                out[i * 4U + c] = bytes_only ? static_cast<std::uint8_t>((pixel & masks[c]) >> shifts[c])
                                  : maxes[c] == 0
                                      ? static_cast<std::uint8_t>(c == 3 ? 255 : 0)
                                      : static_cast<std::uint8_t>(((pixel & masks[c]) >> shifts[c]) * 255U / maxes[c]);
            }
        }
        return texture;
    }
    std::string code;
    for (int i = 0; i < 4; ++i) {
        const auto ch = static_cast<char>(data[84 + i]);
        code += (ch >= 0x20 && ch < 0x7f) ? ch : '?';
    }
    throw std::runtime_error("DDS kind '" + code + "' not read (DXT5 and plain 32-bit only): " + name);
}

DecodedTexture read_dds(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        const auto text = path.u8string();
        throw std::runtime_error("cannot open " + std::string(text.begin(), text.end()));
    }
    // read in one go (an istreambuf_iterator takes it a byte at a time, the most of an HD model's load)
    file.seekg(0, std::ios::end);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<std::streamoff>(0, file.tellg())));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    const auto stem = path.stem().u8string();
    return decode_dds(bytes.data(), bytes.size(), std::string(stem.begin(), stem.end()));
}

}  // namespace khdays::assets
