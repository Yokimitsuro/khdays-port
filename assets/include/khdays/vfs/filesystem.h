#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

// The game filesystem (VFS): a game path ("/db/db_en.p2") resolved to a real file in the extracted
// data -- mod overrides first, then the decompressed and raw NitroFS views -- handed back as raw bytes.
namespace khdays::vfs {

// Set (or auto-detect) the extracted-data root — the directory that contains
// the `nitrofs/` and `decompressed/` views (e.g. `data/extracted/<hash>`).
void set_data_root(const std::filesystem::path& root);
const std::filesystem::path& data_root();

// Find the data root under `extracted_dir` (the single hash subdirectory that
// holds a `nitrofs/`). Returns true and sets the root on success.
bool autodetect_data_root(const std::filesystem::path& extracted_dir = "data/extracted");

// Set the mods root (default "mods"). Each mod may shadow game files under
// `mods/<Mod>/files/<game_path>`.
void set_mods_root(const std::filesystem::path& root);

// The mods read, in load order (the port's runtime/mods/list.h: the folders order.ini
// turns on). Set, it replaces the listing of the mods root; empty, nothing is read.
void set_mods(std::vector<std::filesystem::path> mods);

// A mod folder that is off by hand, whatever the order says: its name begins
// with '.' or it holds a file named ".ignore".
bool mod_ignored(const std::filesystem::path& mod);

// A file some mod provides at `mods/<Mod>/<rel>` (e.g.
// "sounds/strm/Title_BGM_PCM8.ogg"): the first mod, in load order (the list
// set_mods gave, else the root's folders in sorted order, the ignored ones
// left out), that has it -- the order every override follows.
std::optional<std::filesystem::path> find_in_mods(const std::filesystem::path& rel);

// Resolve a game path to an on-disk file, searching in order: mod overrides,
// the decompressed view, then raw NitroFS. Returns
// std::nullopt if nothing matches. A path escaping the root (via "..") is
// rejected.
std::optional<std::filesystem::path> resolve(std::string_view game_path);

// Resolve a game path to the file as the game itself reads it: a mod override,
// else the raw NitroFS file -- never the extractor's decompressed view, whose
// bytes the game would not recognise (it decompresses on its own). This is what the recompiled game's ROM is built from,
// so a mod there must be in the game's own format.
std::optional<std::filesystem::path> resolve_original(std::string_view game_path);

// True if resolve() finds the game path.
bool exists(std::string_view game_path);

// Read a game file's raw bytes. Throws std::runtime_error if it cannot be
// resolved or read.
std::vector<std::uint8_t> read(std::string_view game_path);

}  // namespace khdays::vfs
