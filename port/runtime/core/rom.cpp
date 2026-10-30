// The game card's ROM, rebuilt from the extracted data (rom.h): read through the card bus as on the DS,
// each file through the VFS so a mod replaces it. The layout is the original's (a bigger mod file past
// the end, the FAT patched); gaps read 0xff; the three ranges the extraction drops stop the process.
#include "rom.h"
#include "text_patches.h"

extern "C" {
#include "../../host/app/host.h"     // khdays_host_tell_no_data
#include "../../host/app/instance.h" // khdays_host_user_folder
#include "../mods/days.h"
#include "../mods/list.h"
#include "origin.h"
#include "session_log.h"
}
#include "../menus/saveworld.h"

#include "khdays/assets/message.h" // lz_decompress
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
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

// A path from UTF-8 text (fs::u8path's, deprecated in C++20).
fs::path utf8_path(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

struct Region {
    u32 start;
    u32 end;
    int kind;  // what serves it: see read_region
    int index; // file id for kind GAME_FILE
};

enum { HEADER, SYSTEM_BLOB, FAT, GAME_FILE, UNEXTRACTED };

std::vector<u8> header;
std::vector<u8> fat; // as the game reads it (moved mod files patched in)
struct Blob {
    u32 start;
    std::vector<u8> bytes;
};
std::vector<Blob> blobs; // arm9, arm7, overlay table, fnt
constexpr int OVERLAY_TABLE_BLOB = 2;

// The header's game code and ROM version of the US release whose layout rom.h describes.
constexpr char US_GAME_CODE[] = "YKGE";
constexpr u32 GAME_CODE_AT = 0x0c;
constexpr u32 ROM_VERSION_AT = 0x1e;
constexpr u8 US_ROM_VERSION = 0;
bool us_card;

// A US card's overlay table at the EU build's addresses: each 32-byte entry's RAM address and its
// static initializers' start and end (fields 1, 4 and 5; an empty one stays 0). Its file id and
// compressed size are the card's own.
void move_us_overlay_table(std::vector<u8> &table)
{
    constexpr u32 ENTRY = 32;
    constexpr int SHIFTED[] = {1, 4, 5};
    for (size_t at = 0; at + ENTRY <= table.size(); at += ENTRY) {
        for (const int field : SHIFTED) {
            u32 value;
            std::memcpy(&value, table.data() + at + 4 * field, 4);
            if (value != 0) {
                value += KHDAYS_US_SHIFT;
                std::memcpy(table.data() + at + 4 * field, &value, 4);
            }
        }
    }
}

struct GameFile {
    fs::path path; // resolved through the VFS
    u32 size;
    std::vector<u8> bytes; // the port's wording of a file (text_patches.h), served from memory
};
std::vector<GameFile> files;
std::vector<std::string> game_paths; // by file id: the FNT's name, or _unnamed/file_NNNNN.bin
std::map<std::string, u32> file_ids; // the game's paths, as the FNT names them
std::vector<Region> regions;         // sorted by start
u32 image_end;                       // past everything placed

// The files the mods add (khdays_rom_add_file), by their paths in lower case: the image's range and id.
struct Added {
    u32 top, bottom, id;
};
std::map<std::string, Added> added;

std::string lower(std::string_view text)
{
    std::string s(text);
    for (char &c : s) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return s;
}

u32 le32(const u8 *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<u32>(p[3]) << 24;
}
u16 le16(const u8 *p)
{
    return static_cast<u16>(p[0] | p[1] << 8);
}

// A path as UTF-8 (the message box's).
std::string utf8(const fs::path &p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

[[noreturn]] void fail(const std::string &what)
{
    std::fprintf(stderr, "rom: %s\n", what.c_str());
    std::fflush(stderr);
    // the player told too (a window's message box: the port may run with no console)
    std::error_code ec;
    const fs::path root = fs::absolute(khdays::vfs::data_root(), ec);
    khdays_host_tell_no_data(utf8(ec ? khdays::vfs::data_root() : root).c_str(), what.c_str());
    std::exit(8);
}

std::vector<u8> read_whole(const fs::path &path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        fail("cannot read " + path.string());
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

// FNT: file id -> game path.
void walk_fnt(const std::vector<u8> &fnt, u32 dir, const std::string &prefix, std::map<u32, std::string> &names)
{
    const u8 *entry = fnt.data() + 8 * (dir & 0xfff);
    const u8 *p = fnt.data() + le32(entry);
    u32 id = le16(entry + 4);
    for (;;) {
        const u8 type = *p++;
        if (type == 0) {
            return;
        }
        const std::string name(reinterpret_cast<const char *>(p), type & 0x7f);
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

} // namespace

// Open files, most recently used first. The game reads a file in 0x200-byte blocks and opening costs
// most (a field's load reads about a hundred files), so up to 192 stay open, each with a buffer of up
// to 64 KB; a read that goes on where the last ended does not seek. Each thread has a reader of its
// own.
struct OpenFile {
    int id;
    std::vector<char> buffer;
    std::ifstream in;
    u32 at = ~0u; // where the next read would start without a seek
};
struct KhdaysRomReader {
    std::list<OpenFile> open_files;
};

namespace {

KhdaysRomReader card_reader;

OpenFile &handle(KhdaysRomReader &reader, int id)
{
    auto &open_files = reader.open_files;
    for (auto it = open_files.begin(); it != open_files.end(); ++it) {
        if (it->id == id) {
            open_files.splice(open_files.begin(), open_files, it);
            return open_files.front();
        }
    }
    if (open_files.size() >= 192) {
        open_files.pop_back();
    }
    open_files.emplace_front();
    OpenFile &f = open_files.front();
    f.id = id;
    f.buffer.resize(std::min<std::size_t>(64 * 1024, files[id].size + 1u));
    f.in.rdbuf()->pubsetbuf(f.buffer.data(), static_cast<std::streamsize>(f.buffer.size()));
    f.in.open(files[id].path, std::ios::binary);
    if (!f.in) {
        fail("cannot open " + files[id].path.string());
    }
    return f;
}

void read_region(KhdaysRomReader &reader, const Region &r, u32 offset, u8 *out, u32 size)
{
    const u32 at = offset - r.start;
    switch (r.kind) {
    case HEADER: std::memcpy(out, header.data() + at, size); return;
    case SYSTEM_BLOB: std::memcpy(out, blobs[r.index].bytes.data() + at, size); return;
    case FAT: std::memcpy(out, fat.data() + at, size); return;
    case GAME_FILE: {
        if (!files[r.index].bytes.empty()) {
            std::memcpy(out, files[r.index].bytes.data() + at, size);
            return;
        }
        OpenFile &f = handle(reader, r.index);
        if (f.at != at || !f.in) {
            f.in.clear();
            f.in.seekg(at);
        }
        f.in.read(reinterpret_cast<char *>(out), size);
        f.at = f.in ? at + size : ~0u;
        return;
    }
    default: {
        char message[96];
        std::snprintf(message, sizeof(message), "the game read ROM bytes the extraction does not keep (0x%08x)",
                      offset);
        fail(message);
    }
    }
}

} // namespace

// The VFS reads the mods on as the game started, in the list's order (runtime/mods/list.h).
static void give_vfs_the_mods()
{
    std::vector<fs::path> active;
    for (int i = 0; i < khdays_mods_started_count(); ++i) {
        if (const char *path = khdays_mods_started_path(i)) {
            active.push_back(utf8_path(path));
        }
    }
    khdays::vfs::set_mods(std::move(active));
}

extern "C" int khdays_rom_init(void)
{
    khdays_days_unpack_all("mods"); // the .days mods' files copied out where the VFS reads them
    khdays_mods_scan("mods", khdays_host_user_folder()); // the VFS's mods root, beside the game
    khdays_mods_start();
    give_vfs_the_mods();
    if (!khdays::vfs::autodetect_data_root()) {
        std::error_code ec;
        const fs::path searched = fs::absolute("data/extracted", ec);
        std::fprintf(stderr, "rom: no extracted data under data/extracted\n");
        khdays_host_tell_no_data(utf8(ec ? fs::path("data/extracted") : searched).c_str(), nullptr);
        return 0;
    }
    khdays_days_check_originals(utf8(khdays::vfs::data_root() / "nitrofs").c_str());
    const fs::path system = khdays::vfs::data_root() / "system";
    header = read_whole(system / "header.bin");
    if (header.size() != 0x200) {
        fail("system/header.bin is not a 0x200-byte cartridge header (re-run the extractor)");
    }
    const auto field = [](u32 offset) { return le32(header.data() + offset); };
    us_card =
        std::memcmp(header.data() + GAME_CODE_AT, US_GAME_CODE, 4) == 0 && header[ROM_VERSION_AT] == US_ROM_VERSION;

    const struct {
        const char *name;
        u32 offset_field;
        u32 size_field;
    } system_files[] = {
        {"arm9.bin", 0x20, 0x2c},
        {"arm7.bin", 0x30, 0x3c},
        {"arm9_overlay_table.bin", 0x50, 0x54},
        {"fnt.bin", 0x40, 0x44},
    };
    regions.push_back({0, 0x200, HEADER, 0});
    for (const auto &s : system_files) {
        Blob blob{field(s.offset_field), read_whole(system / s.name)};
        if (blob.bytes.size() != field(s.size_field)) {
            fail(std::string("system/") + s.name + " does not match the header's size");
        }
        regions.push_back({blob.start, blob.start + static_cast<u32>(blob.bytes.size()), SYSTEM_BLOB,
                           static_cast<int>(blobs.size())});
        blobs.push_back(std::move(blob));
    }
    if (us_card) {
        move_us_overlay_table(blobs[OVERLAY_TABLE_BLOB].bytes);
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
    for (const auto &[id, name] : names) {
        file_ids[name] = id;
    }
    const u32 count = static_cast<u32>(fat.size() / 8);
    u32 rom_end = field(0x80);
    files.resize(count);
    game_paths.resize(count);
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
        // a mission world a mod replaces (the studio's map editor): its rooms' models named from the start,
        // so the mod's are drawn even with HD graphics off (models.cpp)
        if (game_path.rfind("mi/wd/wd_", 0) == 0 &&
            khdays::vfs::find_in_mods(fs::path("files") / utf8_path(game_path))) {
            khdays_origin_want();
        }
        std::vector<u8> worded;
        if (khdays_text_patch(game_path, *path, worded)) {
            const u32 bytes = static_cast<u32>(worded.size());
            files[id] = {*path, bytes, std::move(worded)};
        } else {
            files[id] = {*path, static_cast<u32>(fs::file_size(*path)), {}};
        }
        const u32 size = files[id].size;
        game_paths[id] = game_path;
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
    std::sort(regions.begin(), regions.end(), [](const Region &a, const Region &b) { return a.start < b.start; });
    image_end = rom_end;
    return 1;
}

namespace {

// A file the game does not have placed past everything else: read from `where`, or served from `bytes`.
int add(const char *path, const fs::path &where, std::uint64_t size, std::vector<u8> bytes)
{
    if (size == 0 || size > 0xffffffffull - image_end - 0x200u) {
        return 0;
    }
    const u32 placed = (image_end + 0x1ff) & ~0x1ffu;
    const u32 id = static_cast<u32>(files.size());
    image_end = placed + static_cast<u32>(size);
    files.push_back({where, static_cast<u32>(size), std::move(bytes)});
    game_paths.emplace_back(path);
    regions.push_back({placed, image_end, GAME_FILE, static_cast<int>(id)}); // the last: still in order
    added[lower(path)] = {placed, image_end, id};
    return 1;
}

} // namespace

extern "C" int khdays_rom_add_file(const char *path, const char *disk)
{
    const fs::path where = utf8_path(disk);
    std::error_code ec;
    const auto size = fs::file_size(where, ec);
    return !ec && add(path, where, size, {});
}

extern "C" int khdays_rom_add_bytes(const char *path, const unsigned char *data, unsigned int size)
{
    return add(path, {}, size, std::vector<u8>(data, data + size));
}

extern "C" int khdays_rom_add_alias(const char *path, const char *game_path)
{
    const auto it = file_ids.find(game_path);
    if (it == file_ids.end()) {
        return 0;
    }
    const u32 id = it->second;
    added[lower(path)] = {le32(fat.data() + 8 * id), le32(fat.data() + 8 * id + 4), id};
    return 1;
}

extern "C" int khdays_rom_added_file(const char *path, unsigned int *bounds)
{
    if (added.empty()) {
        return 0;
    }
    while (*path == '/') {
        ++path;
    }
    const auto it = added.find(lower(path));
    if (it == added.end()) {
        return 0;
    }
    bounds[0] = it->second.top;
    bounds[1] = it->second.bottom;
    bounds[2] = it->second.id;
    khdays_log("rom: the game opened %s, a mod's (the image's 0x%08x-0x%08x)", path, it->second.top, it->second.bottom);
    return 1;
}

extern "C" const u8 *khdays_rom_header(void)
{
    return header.data();
}

extern "C" int khdays_rom_is_us(void)
{
    return us_card ? 1 : 0;
}

extern "C" int khdays_rom_language(int language)
{
    constexpr int ENGLISH = 1;
    constexpr int FRENCH = 2;
    return us_card && language != ENGLISH && language != FRENCH ? ENGLISH : language;
}

namespace {

// The regions are read-only once khdays_rom_init has built them, so any
// thread can read them with a reader of its own.
void read_rom(KhdaysRomReader &reader, u32 offset, u8 *destination, u32 size)
{
    while (size != 0) {
        // The last region starting at or before offset.
        auto it = std::upper_bound(regions.begin(), regions.end(), offset,
                                   [](u32 value, const Region &r) { return value < r.start; });
        u32 chunk;
        if (it != regions.begin() && offset < std::prev(it)->end) {
            const Region &r = *std::prev(it);
            chunk = std::min(size, r.end - offset);
            read_region(reader, r, offset, destination, chunk);
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

} // namespace

extern "C" void khdays_rom_read(u32 offset, u8 *destination, u32 size)
{
    khdays_saveworld_card_read(offset); /* the mission worlds' archives read: the save list's world */
    read_rom(card_reader, offset, destination, size);
}

extern "C" KhdaysRomReader *khdays_rom_reader_new(void)
{
    return new KhdaysRomReader;
}

extern "C" void khdays_rom_reader_read(KhdaysRomReader *reader, u32 offset, u8 *destination, u32 size)
{
    read_rom(*reader, offset, destination, size);
}

extern "C" int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, u8 **data, u32 *size)
{
    u32 top = 0, bytes = 0;
    std::vector<u8> out;
    bool packed = false;
    *data = nullptr;
    *size = 0;
    if (!khdays_rom_find(path, &top, &bytes)) {
        return 0;
    }
    if (entry < 0) {
        const std::size_t n = std::strlen(path);
        out.resize(bytes);
        read_rom(*reader, top, out.data(), bytes);
        packed = n > 2 && std::strcmp(path + n - 2, ".z") == 0;
    } else {
        u8 head[0x10], at[2], raw[4];
        if (bytes < sizeof(head)) {
            return 0;
        }
        read_rom(*reader, top, head, sizeof(head));
        const u32 count = le16(head + 2) & 0x1ffu;
        if (head[0] != 'P' || head[1] != '2' || static_cast<u32>(entry) >= count) {
            return 0;
        }
        read_rom(*reader, top + 0x10 + 2 * static_cast<u32>(entry), at, sizeof(at));
        read_rom(*reader, top + 0x10 + (count + 1) / 2 * 4 + 4 * static_cast<u32>(entry), raw, sizeof(raw));
        const u32 within = le32(head + 0xc) + (static_cast<u32>(le16(at)) << 9);
        const u32 length = le32(raw) & 0x7fffffffu;
        if (within > bytes || length > bytes - within) {
            return 0;
        }
        out.resize(length);
        read_rom(*reader, top + within, out.data(), length);
        packed = (le32(raw) & 0x80000000u) != 0;
    }
    if (packed) {
        try {
            out = khdays::assets::lz_decompress(out);
        } catch (...) {
            return 0;
        }
    }
    *data = static_cast<u8 *>(std::malloc(out.empty() ? 1 : out.size()));
    if (*data == nullptr) {
        return 0;
    }
    if (!out.empty()) {
        std::memcpy(*data, out.data(), out.size());
    }
    *size = static_cast<u32>(out.size());
    return 1;
}

extern "C" int khdays_rom_find(const char *path, u32 *offset, u32 *size)
{
    while (*path == '/') {
        ++path;
    }
    const auto it = file_ids.find(path);
    if (it == file_ids.end()) {
        const auto more = added.find(lower(path)); // a file a mod adds
        if (more == added.end()) {
            return 0;
        }
        *offset = more->second.top;
        *size = more->second.bottom - more->second.top;
        return 1;
    }
    *offset = le32(fat.data() + 8 * it->second);
    *size = le32(fat.data() + 8 * it->second + 4) - *offset;
    return 1;
}

extern "C" int khdays_rom_file_count(void)
{
    return static_cast<int>(fat.size() / 8); // the table's files, not the mods' added ones
}

extern "C" int khdays_rom_file_info(int id, char *path, int size, u32 *offset, u32 *bytes)
{
    if (id < 0 || id >= khdays_rom_file_count()) {
        return 0;
    }
    const std::string &name = game_paths[static_cast<std::size_t>(id)];
    if (name.size() + 1 > static_cast<std::size_t>(size)) {
        return 0;
    }
    std::memcpy(path, name.c_str(), name.size() + 1);
    *offset = le32(fat.data() + 8 * id);
    *bytes = le32(fat.data() + 8 * id + 4) - *offset;
    return 1;
}

extern "C" int khdays_rom_file_at(u32 offset, char *path, int size, u32 *within)
{
    const auto it = std::upper_bound(regions.begin(), regions.end(), offset,
                                     [](u32 value, const Region &r) { return value < r.start; });
    if (it == regions.begin() || offset >= std::prev(it)->end || std::prev(it)->kind != GAME_FILE) {
        return 0;
    }
    const Region &r = *std::prev(it);
    const std::string &name = game_paths[static_cast<std::size_t>(r.index)];
    if (name.size() + 1 > static_cast<std::size_t>(size)) {
        return 0;
    }
    std::memcpy(path, name.c_str(), name.size() + 1);
    *within = offset - r.start;
    return 1;
}

extern "C" int khdays_rom_area_at(u32 offset, char *name, int size, u32 *start, u32 *end)
{
    // the system blobs in the order khdays_rom_init reads them
    static const char *const blob_names[] = {"(the ARM9 binary)", "(the ARM7 binary)", "(the overlay table)",
                                             "(the FNT)"};
    const auto it = std::upper_bound(regions.begin(), regions.end(), offset,
                                     [](u32 value, const Region &r) { return value < r.start; });
    if (it == regions.begin() || offset >= std::prev(it)->end) {
        return 0;
    }
    const Region &r = *std::prev(it);
    std::string what;
    switch (r.kind) {
    case HEADER: what = "(the header)"; break;
    case SYSTEM_BLOB: what = r.index >= 0 && r.index < 4 ? blob_names[r.index] : "(a system file)"; break;
    case FAT: what = "(the FAT)"; break;
    case GAME_FILE: what = game_paths[static_cast<std::size_t>(r.index)]; break;
    default: return 0;
    }
    if (what.size() + 1 > static_cast<std::size_t>(size)) {
        return 0;
    }
    std::memcpy(name, what.c_str(), what.size() + 1);
    *start = r.start;
    *end = r.end;
    return 1;
}

extern "C" int khdays_mods_find(const char *relative, char *path, int size)
{
    const auto found = khdays::vfs::find_in_mods(utf8_path(relative));
    if (!found) {
        return 0;
    }
    const auto utf8 = found->u8string();
    const std::string text(utf8.begin(), utf8.end());
    if (text.size() + 1 > static_cast<std::size_t>(size)) {
        return 0;
    }
    std::memcpy(path, text.c_str(), text.size() + 1);
    return 1;
}
