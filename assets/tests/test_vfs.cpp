#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/vfs/filesystem.h"

namespace {

namespace fs = std::filesystem;

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out << content;
}

std::string read_string(const std::vector<std::uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    const auto base = fs::temp_directory_path() / "khdays_vfs_test";
    const auto root = base / "data";
    const auto mods = base / "mods";
    try {
        fs::remove_all(base);
        // Same game path present in both data views with distinct content.
        write_file(root / "nitrofs" / "a" / "x.bin", "nitro");
        write_file(root / "decompressed" / "a" / "x.bin", "decomp");
        // A file only in nitrofs.
        write_file(root / "nitrofs" / "only.bin", "raw");

        khdays::vfs::set_data_root(root);
        khdays::vfs::set_mods_root(mods);

        // Decompressed wins over nitrofs.
        expect(read_string(khdays::vfs::read("a/x.bin")) == "decomp", "view priority");
        // Leading slash is normalized.
        expect(read_string(khdays::vfs::read("/a/x.bin")) == "decomp", "leading slash");
        // Falls through to nitrofs when that is the only view.
        expect(read_string(khdays::vfs::read("only.bin")) == "raw", "nitrofs fallback");

        // The game's own view skips the extractor's decompressed
        // copies and reads the raw NitroFS file.
        expect(khdays::vfs::resolve_original("a/x.bin") == root / "nitrofs" / "a" / "x.bin",
               "original view is raw NitroFS");
        expect(!khdays::vfs::resolve_original("missing.bin").has_value(), "original view: missing file");

        // A mod file shadows every data view.
        write_file(mods / "ZMod" / "files" / "a" / "x.bin", "mod");
        expect(read_string(khdays::vfs::read("a/x.bin")) == "mod", "mod override");
        expect(khdays::vfs::resolve_original("a/x.bin") == mods / "ZMod" / "files" / "a" / "x.bin",
               "original view: mod override");

        // A mod off by hand is not read: a .ignore file, or a name beginning with a dot.
        write_file(mods / "Ign" / "files" / "a" / "x.bin", "ignored");
        write_file(mods / "Ign" / ".ignore", "");
        write_file(mods / ".Hidden" / "files" / "a" / "x.bin", "hidden");
        expect(khdays::vfs::mod_ignored(mods / "Ign") && khdays::vfs::mod_ignored(mods / ".Hidden") &&
                   !khdays::vfs::mod_ignored(mods / "ZMod"),
               "mod_ignored");
        expect(read_string(khdays::vfs::read("a/x.bin")) == "mod", "ignored mods skipped");

        // The list set_mods gives replaces the folder's listing: its order, and only its mods.
        write_file(mods / "AMod" / "files" / "a" / "x.bin", "amod");
        expect(read_string(khdays::vfs::read("a/x.bin")) == "amod", "sorted order: AMod first");
        khdays::vfs::set_mods({mods / "ZMod", mods / "AMod"});
        expect(read_string(khdays::vfs::read("a/x.bin")) == "mod", "the list's order: ZMod first");
        khdays::vfs::set_mods({});
        expect(read_string(khdays::vfs::read("a/x.bin")) == "decomp", "an empty list: no mod read");
        khdays::vfs::set_mods_root(mods);  // the listing again
        expect(read_string(khdays::vfs::read("a/x.bin")) == "amod", "the root set again: listed again");

        // Missing files and path traversal.
        expect(!khdays::vfs::exists("does/not/exist"), "missing file");
        expect(!khdays::vfs::resolve("../escape").has_value(), "traversal rejected");
        // An absolute path to a real file outside the root must not resolve
        // either (on Windows it carries a drive root name).
        const auto outside = base / "outside.bin";
        write_file(outside, "outside");
        expect(!khdays::vfs::resolve(outside.generic_string()).has_value(), "absolute path rejected");

        bool threw = false;
        try {
            khdays::vfs::read("nope");
        } catch (const std::exception&) {
            threw = true;
        }
        expect(threw, "read throws on missing");

        fs::remove_all(base);
        std::cout << "VFS test passed\n";
        return 0;
    } catch (const std::exception& error) {
        fs::remove_all(base);
        std::cerr << "VFS test failed: " << error.what() << '\n';
        return 1;
    }
}
