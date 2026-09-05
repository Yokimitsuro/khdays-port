#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/resource/mission.h"
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
        // Same game path present in all three data views with distinct content.
        write_file(root / "nitrofs" / "a" / "x.bin", "nitro");
        write_file(root / "decompressed" / "a" / "x.bin", "decomp");
        write_file(root / "decompressed" / "unpacked" / "a" / "x.bin", "unpacked");
        // A file only in nitrofs.
        write_file(root / "nitrofs" / "only.bin", "raw");

        khdays::vfs::set_data_root(root);
        khdays::vfs::set_mods_root(mods);

        // Unpacked wins over decompressed over nitrofs.
        expect(read_string(khdays::vfs::read("a/x.bin")) == "unpacked",
               "view priority");
        // Leading slash is normalized.
        expect(read_string(khdays::vfs::read("/a/x.bin")) == "unpacked",
               "leading slash");
        // Falls through to nitrofs when that is the only view.
        expect(read_string(khdays::vfs::read("only.bin")) == "raw",
               "nitrofs fallback");

        // A mod file shadows every data view.
        write_file(mods / "ZMod" / "files" / "a" / "x.bin", "mod");
        expect(read_string(khdays::vfs::read("a/x.bin")) == "mod",
               "mod override");

        // Missing files and path traversal.
        expect(!khdays::vfs::exists("does/not/exist"), "missing file");
        expect(!khdays::vfs::resolve("../escape").has_value(),
               "traversal rejected");

        bool threw = false;
        try {
            khdays::vfs::read("nope");
        } catch (const std::exception&) {
            threw = true;
        }
        expect(threw, "read throws on missing");

        // Mission P2 archives carry fixed eight-byte script names after their
        // descriptors. The selected `<day>.Z` CAKP contains the MobiClip path.
        std::vector<std::uint8_t> mission(0x240U, 0U);
        mission[0] = 'P';
        mission[1] = '2';
        mission[2] = 1U;
        mission[3] = 0x80U;
        mission[0x0dU] = 2U;  // payload base = 0x200
        const std::string script_name = "400.Z";
        std::copy(script_name.begin(), script_name.end(),
                  mission.begin() + 0x18);
        const std::string movie_path = "/mv/818.mods";
        std::copy(movie_path.begin(), movie_path.end(),
                  mission.begin() + 0x210);
        const auto movie = khdays::resource::story_movie_reference(
            mission, 400U);
        expect(movie && *movie == "mv/818.mods", "story movie lookup");
        expect(!khdays::resource::story_movie_reference(mission, 7U),
               "missing story day");

        fs::remove_all(base);
        std::cout << "VFS test passed\n";
        return 0;
    } catch (const std::exception& error) {
        fs::remove_all(base);
        std::cerr << "VFS test failed: " << error.what() << '\n';
        return 1;
    }
}
