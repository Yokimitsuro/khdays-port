#include "khdays/assets/message.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Reader for the "P2" message container (db/db_<lang>.p2) and its per-sub-file
// Nintendo DS LZ compression. The container format and the LZ11 sub-file
// packing were confirmed from the decompiled loader (Archive_LoadFile /
// Msg_OpenContainerAndReadHeader) and byte-checked against db_en.p2.

namespace khdays::assets {

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t off) {
    if (off + 2U > d.size()) {
        throw std::runtime_error("P2: truncated u16 read");
    }
    return static_cast<std::uint16_t>(d[off] | (d[off + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t off) {
    if (off + 4U > d.size()) {
        throw std::runtime_error("P2: truncated u32 read");
    }
    return static_cast<std::uint32_t>(d[off]) | (static_cast<std::uint32_t>(d[off + 1U]) << 8U) |
           (static_cast<std::uint32_t>(d[off + 2U]) << 16U) | (static_cast<std::uint32_t>(d[off + 3U]) << 24U);
}

}  // namespace

std::vector<std::uint8_t> lz_decompress(const ByteVector& input) {
    if (input.size() < 4U) {
        throw std::runtime_error("LZ: stream too small");
    }
    const auto type = input[0];
    if (type != 0x10U && type != 0x11U) {
        throw std::runtime_error("LZ: unsupported compression type");
    }

    std::size_t pos = 4U;
    std::size_t out_size = static_cast<std::size_t>(input[1]) | (static_cast<std::size_t>(input[2]) << 8U) |
                           (static_cast<std::size_t>(input[3]) << 16U);
    if (out_size == 0U) {  // extended 32-bit size
        out_size = read_u32(input, 4U);
        pos = 8U;
    }

    const auto byte_at = [&](const std::size_t p) -> std::uint8_t {
        if (p >= input.size()) {
            throw std::runtime_error("LZ: read past end of stream");
        }
        return input[p];
    };

    ByteVector out;
    out.reserve(out_size);
    while (out.size() < out_size) {
        const auto flags = byte_at(pos++);
        for (int bit = 0; bit < 8 && out.size() < out_size; ++bit) {
            if ((flags & (0x80U >> bit)) == 0U) {
                out.push_back(byte_at(pos++));  // literal
                continue;
            }
            // Back-reference.
            std::size_t length = 0U;
            std::size_t disp = 0U;
            if (type == 0x11U) {
                const auto b0 = byte_at(pos);
                const auto indicator = static_cast<unsigned>(b0 >> 4U);
                if (indicator == 0U) {
                    const auto b1 = byte_at(pos + 1U);
                    const auto b2 = byte_at(pos + 2U);
                    length = ((b0 << 4U) | (b1 >> 4U)) + 0x11U;
                    disp = (((b1 & 0x0FU) << 8U) | b2) + 1U;
                    pos += 3U;
                } else if (indicator == 1U) {
                    const auto b1 = byte_at(pos + 1U);
                    const auto b2 = byte_at(pos + 2U);
                    const auto b3 = byte_at(pos + 3U);
                    length = (((b0 & 0x0FU) << 12U) | (b1 << 4U) | (b2 >> 4U)) + 0x111U;
                    disp = (((b2 & 0x0FU) << 8U) | b3) + 1U;
                    pos += 4U;
                } else {
                    const auto b1 = byte_at(pos + 1U);
                    length = indicator + 1U;
                    disp = (((b0 & 0x0FU) << 8U) | b1) + 1U;
                    pos += 2U;
                }
            } else {  // LZ10
                const auto b0 = byte_at(pos);
                const auto b1 = byte_at(pos + 1U);
                length = (b0 >> 4U) + 3U;
                disp = (((b0 & 0x0FU) << 8U) | b1) + 1U;
                pos += 2U;
            }
            if (disp > out.size()) {
                throw std::runtime_error("LZ: back-reference before output");
            }
            for (std::size_t i = 0U; i < length && out.size() < out_size; ++i) {
                out.push_back(out[out.size() - disp]);
            }
        }
    }
    return out;
}

ByteVector lz11_literals(const ByteVector& raw) {
    ByteVector out;
    const auto size = static_cast<std::uint32_t>(raw.size());
    out.push_back(0x11U);
    if (size <= 0xFFFFFFU) {
        out.push_back(static_cast<std::uint8_t>(size));
        out.push_back(static_cast<std::uint8_t>(size >> 8U));
        out.push_back(static_cast<std::uint8_t>(size >> 16U));
    } else {  // extended 32-bit size
        for (int i = 0; i < 3; ++i) {
            out.push_back(0U);
        }
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(size >> (8U * static_cast<unsigned>(i))));
        }
    }
    for (std::size_t i = 0U; i < raw.size(); i += 8U) {
        out.push_back(0U);
        for (std::size_t j = i; j < raw.size() && j < i + 8U; ++j) {
            out.push_back(raw[j]);
        }
    }
    while (out.size() % 4U != 0U) {
        out.push_back(0U);
    }
    return out;
}

std::string message_to_utf8(const std::u16string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0U; i < text.size(); ++i) {
        char32_t cp = text[i];
        // Combine a UTF-16 surrogate pair, if present.
        if (cp >= 0xD800U && cp <= 0xDBFFU && i + 1U < text.size() && text[i + 1U] >= 0xDC00U &&
            text[i + 1U] <= 0xDFFFU) {
            cp = 0x10000U + ((cp - 0xD800U) << 10U) + (text[i + 1U] - 0xDC00U);
            ++i;
        }
        if (cp < 0x20U && cp != u'\n' && cp != u'\t') {
            static const char* hex = "0123456789abcdef";
            out += "\\x";
            out += hex[(cp >> 4U) & 0x0FU];
            out += hex[cp & 0x0FU];
        } else if (cp < 0x80U) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800U) {
            out += static_cast<char>(0xC0U | (cp >> 6U));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        } else if (cp < 0x10000U) {
            out += static_cast<char>(0xE0U | (cp >> 12U));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        } else {
            out += static_cast<char>(0xF0U | (cp >> 18U));
            out += static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (cp & 0x3FU));
        }
    }
    return out;
}

std::u16string message_from_utf8(const std::string& text) {
    std::u16string out;
    const auto emit = [&](char32_t cp) {
        if (cp < 0x10000U) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000U;
            out.push_back(static_cast<char16_t>(0xD800U + (cp >> 10U)));
            out.push_back(static_cast<char16_t>(0xDC00U + (cp & 0x3FFU)));
        }
    };
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    std::size_t i = 0U;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\\' && i + 1U < text.size()) {
            const char e = text[i + 1U];
            if (e == 'n') {
                emit(u'\n');
                i += 2U;
                continue;
            }
            if (e == 't') {
                emit(u'\t');
                i += 2U;
                continue;
            }
            if (e == '\\') {
                emit(u'\\');
                i += 2U;
                continue;
            }
            if (e == 'x' && i + 3U < text.size()) {
                const int h1 = hex(text[i + 2U]);
                const int h2 = hex(text[i + 3U]);
                if (h1 >= 0 && h2 >= 0) {
                    emit(static_cast<char32_t>((h1 << 4) | h2));
                    i += 4U;
                    continue;
                }
            }
            if (e == 'u' && i + 5U < text.size()) {
                const int h1 = hex(text[i + 2U]);
                const int h2 = hex(text[i + 3U]);
                const int h3 = hex(text[i + 4U]);
                const int h4 = hex(text[i + 5U]);
                if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                    emit(static_cast<char32_t>((h1 << 12) | (h2 << 8) | (h3 << 4) | h4));
                    i += 6U;
                    continue;
                }
            }
            emit(u'\\');  // unknown escape: keep the backslash
            i += 1U;
            continue;
        }

        // Decode one UTF-8 code point.
        char32_t cp = 0xFFFDU;
        std::size_t n = 1U;
        if (c < 0x80U) {
            cp = c;
        } else if ((c >> 5U) == 0x6U) {
            cp = c & 0x1FU;
            n = 2U;
        } else if ((c >> 4U) == 0xEU) {
            cp = c & 0x0FU;
            n = 3U;
        } else if ((c >> 3U) == 0x1EU) {
            cp = c & 0x07U;
            n = 4U;
        }
        for (std::size_t k = 1U; k < n && i + k < text.size(); ++k) {
            cp = (cp << 6U) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        emit(cp);
        i += n;
    }
    return out;
}

}  // namespace khdays::assets
