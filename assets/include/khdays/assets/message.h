#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

// u32 count; count * { u32 record_length (incl. this field); u16 utf16le[] }`.
// Throws std::runtime_error on malformed data.
std::vector<std::u16string> load_string_table(const std::filesystem::path& path);

// Decompress a Nintendo DS LZ11 (type 0x11) stream. Also accepts LZ10 (0x10).
// Throws std::runtime_error on a malformed stream. Exposed for reuse by other
// packed assets.
std::vector<std::uint8_t> lz_decompress(const std::vector<std::uint8_t>& input);

// Pack `raw` as LZ11 with every byte a literal (a zero flag byte before each eight), padded to four
// bytes: no smaller, but the game's decompression reads it as it reads the ROM's own.
std::vector<std::uint8_t> lz11_literals(const std::vector<std::uint8_t>& raw);

// Render a decoded UTF-16 message as UTF-8 for display: printable code points
// pass through, '\n'/'\t' are kept, and other control units below 0x20 become
// \xNN escapes.
std::string message_to_utf8(const std::u16string& text);

// Parse a UTF-8 string back into a UTF-16 message, decoding the escapes that
// message_to_utf8 emits (\n, \t, \\, \xNN) plus \uNNNN. The inverse used by the
// modding layer so an edited dump round-trips into the game's string form.
std::u16string message_from_utf8(const std::string& text);

}  // namespace khdays::assets
