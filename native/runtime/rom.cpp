// The game card's ROM, rebuilt from the port's extracted data so the
// recompiled game reads its files through the card bus exactly as on the DS,
// while each file comes through the port's VFS (a mod replaces it).
//
// The layout is the original one: the header, the ARM9/ARM7 binaries, the
// overlay table, FNT and FAT at the offsets the header gives, and every file
// at the offset its FAT entry gives. A mod file of a different size is moved
// past the end of the ROM and the FAT the game reads is changed to match.
//
// Between regions a Nintendo ROM holds 0xff padding (checked against this
// game's ROM: every gap between files is 0xff). Three ranges hold data the
// extraction does not keep -- 0x200-0x4000 (behind the card's secure-area
// redirection, so a normal read cannot reach it), the bytes between the ARM9
// binary and the overlay table, and the banner; reading them stops the
// process rather than inventing their content.
#include "rom.h"

#include "khdays/vfs/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Region {
    u32 start;
    u32 end;
    int kind;       // what serves it: see read_region
    int index;      // file id for kind GAME_FILE
};

enum { HEADER, SYSTEM_BLOB, FAT, GAME_FILE, UNEXTRACTED };

std::vector<u8> header;
std::vector<u8> fat;  // as the game reads it (moved mod files patched in)
struct Blob {
    u32 start;
    std::vector<u8> bytes;
};
std::vector<Blob> blobs;  // arm9, arm7, overlay table, fnt

struct GameFile {
    fs::path path;  // resolved through the VFS
    u32 size;
};
std::vector<GameFile> files;
std::vector<Region> regions;  // sorted by start

u32 le32(const u8* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<u32>(p[3]) << 24;
}
u16 le16(const u8* p) {
    return static_cast<u16>(p[0] | p[1] << 8);
}

[[noreturn]] void fail(const std::string& what) {
    std::fprintf(stderr, "rom: %s\n", what.c_str());
    std::fflush(stderr);
    std::exit(8);
}

std::vector<u8> read_whole(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        fail("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

// FNT: file id -> game path.
void walk_fnt(const std::vector<u8>& fnt, u32 dir, const std::string& prefix,
              std::map<u32, std::string>& names) {
    const u8* entry = fnt.data() + 8 * (dir & 0xfff);
    const u8* p = fnt.data() + le32(entry);
    u32 id = le16(entry + 4);
    for (;;) {
        const u8 type = *p++;
        if (type == 0) {
            return;
        }
        const std::string name(reinterpret_cast<const char*>(p), type & 0x7f);
        p += type & 0x7f;
        if (type & 0x80) {
            const u32 sub = le16(p);
            p += 2;
            walk_fnt(fnt, sub, prefix + name + "/", names);
        } else {
            names[id++] = prefix + name;
        }
    }
}

// Open handles, most recently used first; a game reads a file in 0x200-byte
// blocks, so reopening per block would dominate.
std::list<std::pair<int, std::ifstream>> open_files;

std::ifstream& handle(int id) {
    for (auto it = open_files.begin(); it != open_files.end(); ++it) {
        if (it->first == id) {
            open_files.splice(open_files.begin(), open_files, it);
            return open_files.front().second;
        }
    }
    if (open_files.size() >= 32) {
        open_files.pop_back();
    }
    open_files.emplace_front(id, std::ifstream{files[id].path, std::ios::binary});
    if (!open_files.front().second) {
        fail("cannot open " + files[id].path.string());
    }
    return open_files.front().second;
}

void read_region(const Region& r, u32 offset, u8* out, u32 size) {
    const u32 at = offset - r.start;
    switch (r.kind) {
    case HEADER:
        std::memcpy(out, header.data() + at, size);
        return;
    case SYSTEM_BLOB:
        std::memcpy(out, blobs[r.index].bytes.data() + at, size);
        return;
    case FAT:
        std::memcpy(out, fat.data() + at, size);
        return;
    case GAME_FILE: {
        auto& in = handle(r.index);
        in.clear();
        in.seekg(at);
        in.read(reinterpret_cast<char*>(out), size);
        return;
    }
    default: {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "the game read ROM bytes the extraction does not keep (0x%08x)", offset);
        fail(message);
    }
    }
}

}  // namespace

extern "C" int khdays_rom_init(void) {
    if (!khdays::vfs::autodetect_data_root()) {
        std::fprintf(stderr, "rom: no extracted data under data/extracted\n");
        return 0;
    }
    const fs::path system = khdays::vfs::data_root() / "system";
    header = read_whole(system / "header.bin");
    if (header.size() != 0x200) {
        fail("system/header.bin is not a 0x200-byte cartridge header (re-run the extractor)");
    }
    const auto field = [](u32 offset) { return le32(header.data() + offset); };

    const struct {
        const char* name;
        u32 offset_field;
        u32 size_field;
    } system_files[] = {
        {"arm9.bin", 0x20, 0x2c},
        {"arm7.bin", 0x30, 0x3c},
        {"arm9_overlay_table.bin", 0x50, 0x54},
        {"fnt.bin", 0x40, 0x44},
    };
    regions.push_back({0, 0x200, HEADER, 0});
    for (const auto& s : system_files) {
        Blob blob{field(s.offset_field), read_whole(system / s.name)};
        if (blob.bytes.size() != field(s.size_field)) {
            fail(std::string("system/") + s.name + " does not match the header's size");
        }
        regions.push_back({blob.start, blob.start + static_cast<u32>(blob.bytes.size()),
                           SYSTEM_BLOB, static_cast<int>(blobs.size())});
        blobs.push_back(std::move(blob));
    }
    fat = read_whole(system / "fat.bin");
    regions.push_back({field(0x48), field(0x48) + static_cast<u32>(fat.size()), FAT, 0});

    // Data the extraction does not keep (see the file comment).
    regions.push_back({0x200, 0x4000, UNEXTRACTED, 0});
    regions.push_back({field(0x20) + field(0x2c), field(0x50), UNEXTRACTED, 0});
    if (field(0x68) != 0) {
        const u32 banner = field(0x68);
        // Banner version 1 is 0x840 bytes; later versions are larger, and
        // the start is enough to stop on.
        regions.push_back({banner, banner + 0x840, UNEXTRACTED, 0});
    }

    // Files, by FAT id, through the VFS.
    std::map<u32, std::string> names;
    walk_fnt(blobs[3].bytes, 0xf000, "", names);
    const u32 count = static_cast<u32>(fat.size() / 8);
    u32 rom_end = field(0x80);
    files.resize(count);
    for (u32 id = 0; id < count; ++id) {
        const u32 start = le32(fat.data() + 8 * id);
        const u32 end = le32(fat.data() + 8 * id + 4);
        char unnamed[32];
        std::snprintf(unnamed, sizeof(unnamed), "_unnamed/file_%05u.bin", id);
        const auto it = names.find(id);
        const std::string game_path = it != names.end() ? it->second : unnamed;
        const auto path = khdays::vfs::resolve_original(game_path);
        if (!path) {
            fail("no file for " + game_path);
        }
        const u32 size = static_cast<u32>(fs::file_size(*path));
        files[id] = {*path, size};
        u32 placed = start;
        if (size != end - start) {
            // A mod of another size: past everything else, 0x200-aligned.
            placed = (rom_end + 0x1ff) & ~0x1ffu;
            rom_end = placed + size;
            for (int b = 0; b < 4; ++b) {
                fat[8 * id + b] = static_cast<u8>(placed >> (8 * b));
                fat[8 * id + 4 + b] = static_cast<u8>((placed + size) >> (8 * b));
            }
        }
        if (size != 0) {
            regions.push_back({placed, placed + size, GAME_FILE, static_cast<int>(id)});
        }
    }
    std::sort(regions.begin(), regions.end(),
              [](const Region& a, const Region& b) { return a.start < b.start; });
    return 1;
}

extern "C" const u8* khdays_rom_header(void) {
    return header.data();
}

extern "C" void khdays_rom_read(u32 offset, u8* destination, u32 size) {
    while (size != 0) {
        // The last region starting at or before offset.
        auto it = std::upper_bound(regions.begin(), regions.end(), offset,
                                   [](u32 value, const Region& r) { return value < r.start; });
        u32 chunk;
        if (it != regions.begin() && offset < std::prev(it)->end) {
            const Region& r = *std::prev(it);
            chunk = std::min(size, r.end - offset);
            read_region(r, offset, destination, chunk);
        } else {
            // Padding up to the next region.
            const u32 next = it != regions.end() ? it->start : 0xffffffffu;
            chunk = std::min(size, next - offset);
            std::memset(destination, 0xff, chunk);
        }
        offset += chunk;
        destination += chunk;
        size -= chunk;
    }
}
