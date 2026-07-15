#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

// The game filesystem (VFS). The original game reads its data from the ROM's
// NitroFS by path (e.g. "/db/db_en.p2", "/mi/ch/03/slot_7/0000.nsbmd"). This
// layer resolves such a game path to a real file in the locally extracted data,
// searching mod overrides first, then the unpacked, decompressed, and raw
// NitroFS views produced by the extractor. It hands back raw bytes; callers
// decode (and decompress ".z" blobs) as they already do.
namespace khdays::vfs {

// Set (or auto-detect) the extracted-data root — the directory that contains
// the `nitrofs/` and `decompressed/` views (e.g. `data/extracted/<hash>`).
void set_data_root(const std::filesystem::path& root);
const std::filesystem::path& data_root();

// Find the data root under `extracted_dir` (the single hash subdirectory that
// holds a `nitrofs/`). Returns true and sets the root on success.
bool autodetect_data_root(
    const std::filesystem::path& extracted_dir = "data/extracted");

// Set the mods root (default "mods"). Each mod may shadow game files under
// `mods/<Mod>/files/<game_path>`.
void set_mods_root(const std::filesystem::path& root);

// Resolve a game path to an on-disk file, searching in order: mod overrides,
// the unpacked container view, the decompressed view, then raw NitroFS. Returns
// std::nullopt if nothing matches. A path escaping the root (via "..") is
// rejected.
std::optional<std::filesystem::path> resolve(std::string_view game_path);

// True if resolve() finds the game path.
bool exists(std::string_view game_path);

// Read a game file's raw bytes. Throws std::runtime_error if it cannot be
// resolved or read.
std::vector<std::uint8_t> read(std::string_view game_path);

}  // namespace khdays::vfs
