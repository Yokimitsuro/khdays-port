#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

// A decoded "P2" message container (the game's `db/db_<lang>.p2`). The file is a
// directory of sub-databases; each sub-database is an (optionally LZ11-packed)
// blob of UTF-16LE strings with embedded control codes.
//
// On-disk layout (confirmed from the decompiled loader, see
// docs/MESSAGE_DATA_P2.md):
//   header: u16 'P2'; u16 (count = word & 0x1ff, bit 0x8000 = wide); ...; u32
//           base_offset @0x0C.
//   directory @0x10: u16 sizes[] (start sector of each sub-file, x0x200) then
//           u32 desc[] (bit31 = LZ11-compressed, bits0..30 = size in bytes).
//   sub-file k: [base_offset + sizes[k]*0x200, + (desc[k] & 0x7fffffff)); once
//           decompressed it is {u32 count; u32 offsets[count]; u16 data[]}.
struct MessageArchive final {
    // subdbs[d] holds sub-database d's strings, in order. Each string is raw
    // UTF-16LE code units with control codes preserved; the trailing NUL is
    // stripped.
    std::vector<std::vector<std::u16string>> subdbs;

    // Total string count across every sub-database.
    std::size_t string_count() const;
};

// Load and fully decode a P2 message container. Throws std::runtime_error on a
// malformed header, directory, compression stream, or string blob.
MessageArchive load_p2_archive(const std::filesystem::path& path);

// Decompress a Nintendo DS LZ11 (type 0x11) stream. Also accepts LZ10 (0x10).
// Throws std::runtime_error on a malformed stream. Exposed for reuse by other
// packed assets.
std::vector<std::uint8_t> lz_decompress(const std::vector<std::uint8_t>& input);

// Render a decoded UTF-16 message as UTF-8 for display: printable code points
// pass through, '\n'/'\t' are kept, and other control units below 0x20 become
// \xNN escapes.
std::string message_to_utf8(const std::u16string& text);

}  // namespace khdays::assets
