#include "khdays/vfs/filesystem.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace khdays::vfs {

namespace {

std::filesystem::path g_data_root{};
std::filesystem::path g_mods_root{"mods"};

// Normalize a game path to a relative path: strip leading slashes, unify
// separators, and reject any component that would escape the root.
std::optional<std::filesystem::path> normalize(std::string_view game_path) {
    std::string cleaned;
    cleaned.reserve(game_path.size());
    for (const char c : game_path) {
        cleaned.push_back(c == '\\' ? '/' : c);
    }
    std::size_t start = 0;
    while (start < cleaned.size() && cleaned[start] == '/') {
        ++start;
    }
    const std::filesystem::path rel{cleaned.substr(start)};
    if (rel.empty()) {
        return std::nullopt;
    }
    for (const auto& part : rel) {
        if (part == "..") {
            return std::nullopt;  // no traversal outside the root
        }
    }
    return rel;
}

bool is_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// Mod overrides: mods/<Mod>/files/<rel>, mods visited in sorted order.
std::optional<std::filesystem::path> find_mod_file(
    const std::filesystem::path& rel) {
    std::error_code ec;
    if (!std::filesystem::is_directory(g_mods_root, ec)) {
        return std::nullopt;
    }
    std::vector<std::filesystem::path> mods;
    for (const auto& entry :
         std::filesystem::directory_iterator(g_mods_root, ec)) {
        if (entry.is_directory(ec)) {
            mods.push_back(entry.path());
        }
    }
    std::sort(mods.begin(), mods.end());
    for (const auto& mod : mods) {
        const auto candidate = mod / "files" / rel;
        if (is_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace

void set_data_root(const std::filesystem::path& root) {
    g_data_root = root;
}

const std::filesystem::path& data_root() {
    return g_data_root;
}

void set_mods_root(const std::filesystem::path& root) {
    g_mods_root = root;
}

bool autodetect_data_root(const std::filesystem::path& extracted_dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(extracted_dir, ec)) {
        return false;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(extracted_dir, ec)) {
        if (entry.is_directory(ec)
            && std::filesystem::is_directory(entry.path() / "nitrofs", ec)) {
            g_data_root = entry.path();
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> resolve(std::string_view game_path) {
    const auto rel = normalize(game_path);
    if (!rel) {
        return std::nullopt;
    }

    if (const auto mod = find_mod_file(*rel)) {
        return mod;
    }
    if (!g_data_root.empty()) {
        const std::filesystem::path views[] = {
            g_data_root / "decompressed" / "unpacked" / *rel,
            g_data_root / "decompressed" / *rel,
            g_data_root / "nitrofs" / *rel,
        };
        for (const auto& candidate : views) {
            if (is_file(candidate)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

bool exists(std::string_view game_path) {
    return resolve(game_path).has_value();
}

std::vector<std::uint8_t> read(std::string_view game_path) {
    const auto path = resolve(game_path);
    if (!path) {
        throw std::runtime_error(
            "game file not found: " + std::string{game_path});
    }
    std::ifstream stream{*path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open game file: " + path->string());
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    std::vector<std::uint8_t> data(
        end > 0 ? static_cast<std::size_t>(end) : 0U);
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    return data;
}

}  // namespace khdays::vfs
