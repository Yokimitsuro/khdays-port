/* The rooms the mods add to the game's mission worlds (added.h). */
#include "added.h"

#include "../core/session_log.h"
#include "../mods/list.h"

#include "khdays/assets/message.h"  // lz_decompress, lz11_literals

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct Added {
    std::string mod_path, key, world;
    fs::path model, collision;
    int id = -1, index = -1;              // given at the merge: its id (none of the world's) and its place
    int collision_member = -1, model_member = -1;
};

std::vector<Added> rooms;
bool loaded = false;

fs::path utf8_path(const std::string &s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::vector<unsigned char> read_all(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

unsigned le16(const std::vector<unsigned char> &b, std::size_t at)
{
    return at + 2 <= b.size() ? b[at] | (b[at + 1] << 8) : 0;
}

unsigned le32(const std::vector<unsigned char> &b, std::size_t at)
{
    return at + 4 <= b.size() ? b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (unsigned(b[at + 3]) << 24) : 0;
}

void put16(std::vector<unsigned char> &b, std::size_t at, unsigned v)
{
    b[at] = static_cast<unsigned char>(v);
    b[at + 1] = static_cast<unsigned char>(v >> 8);
}

void put32(std::vector<unsigned char> &b, std::size_t at, unsigned v)
{
    for (int i = 0; i < 4; ++i)
        b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>(v >> (8 * i));
}

// A P2 archive's entries as stored (each one's bytes and its descriptor: bit 31 LZ-packed).
struct P2 {
    unsigned flags = 0;  // the count's word above its 9 bits
    unsigned word4 = 0, word8 = 0;
    std::vector<std::vector<unsigned char>> entries;
    std::vector<unsigned> descs;
};

bool read_p2(const std::vector<unsigned char> &b, P2 *out)
{
    if (b.size() < 0x10 || b[0] != 'P' || b[1] != '2')
        return false;
    const unsigned word = le16(b, 2), count = word & 0x1ff, base = le32(b, 0xc);
    out->flags = word & ~0x1ffu;
    out->word4 = le32(b, 4);
    out->word8 = le32(b, 8);
    const std::size_t sizes = 0x10 + ((count + 1) / 2) * 4;
    for (unsigned e = 0; e < count; ++e) {
        const std::size_t at = base + (static_cast<std::size_t>(le16(b, 0x10 + 2 * e)) << 9);
        const unsigned desc = le32(b, sizes + 4 * e), bytes = desc & 0x7fffffff;
        if (at + bytes > b.size())
            return false;
        out->entries.emplace_back(b.begin() + static_cast<std::ptrdiff_t>(at),
                                  b.begin() + static_cast<std::ptrdiff_t>(at + bytes));
        out->descs.push_back(desc);
    }
    return true;
}

// The archive written: its tables, then each entry on a 0x200-byte sector.
std::vector<unsigned char> write_p2(const P2 &p)
{
    const unsigned count = static_cast<unsigned>(p.entries.size());
    const std::size_t sizes = 0x10 + ((count + 1) / 2) * 4, tables = sizes + 4 * count;
    const std::size_t base = (tables + 0x1ff) & ~std::size_t{0x1ff};
    std::vector<unsigned char> b(base, 0);
    b[0] = 'P';
    b[1] = '2';
    put16(b, 2, p.flags | count);
    put32(b, 4, p.word4);
    put32(b, 8, p.word8);
    put32(b, 0xc, static_cast<unsigned>(base));
    for (unsigned e = 0; e < count; ++e) {
        const std::size_t at = b.size();
        put16(b, 0x10 + 2 * e, static_cast<unsigned>((at - base) >> 9));
        put32(b, sizes + 4 * e, p.descs[e]);
        b.insert(b.end(), p.entries[e].begin(), p.entries[e].end());
        b.resize((b.size() + 0x1ff) & ~std::size_t{0x1ff}, 0);
    }
    return b;
}

// A room record of the room table: 0x20 bytes (id, its exits' count, its objects' count, its collision's member, ...,
// its exits' offset at +0x1c), then its objects' offsets (u32 each, from the record), then its object (u8 flags, u8
// its model's member, u16).
constexpr std::size_t kRecord = 0x20;

}  // namespace

extern "C" void khdays_rooms_load(void)
{
    if (loaded)
        return;
    loaded = true;
    for (int i = 0; i < khdays_mods_started_count(); ++i) {
        const char *path = khdays_mods_started_path(i);
        if (path == nullptr)
            continue;
        const fs::path dir = utf8_path(path);
        std::ifstream list(dir / "rooms" / "rooms.txt", std::ios::binary);
        std::string line;
        int number = 0;
        while (std::getline(list, line)) {
            ++number;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            line = line.substr(0, line.find('#'));
            std::istringstream words(line);
            Added a;
            if (!(words >> a.key))
                continue;
            if (!(words >> a.world) || a.world.size() != 2) {
                khdays_log("rooms: %s/rooms/rooms.txt line %d: not <key> <world>", path, number);
                continue;
            }
            const fs::path folder = dir / "rooms" / utf8_path(a.world) / utf8_path(a.key);
            std::error_code ec;
            for (const char *name : {"room.gltf", "room.glb", "room.fbx"}) {
                if (a.model.empty() && fs::is_regular_file(folder / name, ec))
                    a.model = folder / name;
            }
            a.collision = folder / "collision.bin";
            if (a.model.empty() || !fs::is_regular_file(a.collision, ec)) {
                khdays_log("rooms: %s's room %s has no room model or collision.bin in rooms/%s/%s: left out", path,
                           a.key.c_str(), a.world.c_str(), a.key.c_str());
                continue;
            }
            a.mod_path = path;
            rooms.push_back(std::move(a));
        }
    }
    if (!rooms.empty())
        khdays_log("rooms: %zu rooms added by the mods", rooms.size());
}

bool khdays_rooms_merge(const std::string &game_path, const std::vector<unsigned char> &source,
                        std::vector<unsigned char> &out)
{
    if (game_path.rfind("mi/wd/wd_", 0) != 0)
        return false;
    const std::string world = game_path.substr(9);
    std::vector<Added *> here;
    for (auto &a : rooms) {
        if (a.world == world)
            here.push_back(&a);
    }
    if (here.empty())
        return false;
    P2 p;
    if (!read_p2(source, &p) || p.entries.size() < 2) {
        khdays_log("rooms: %s is no world archive the port reads: its added rooms left out", game_path.c_str());
        return false;
    }
    std::vector<unsigned char> table = p.entries[0];
    if (p.descs[0] & 0x80000000u) {
        try {
            table = khdays::assets::lz_decompress(p.entries[0]);
        } catch (const std::exception &e) {
            khdays_log("rooms: %s's room table not unpacked (%s): its added rooms left out", game_path.c_str(), e.what());
            return false;
        }
    }
    const unsigned count = table.empty() ? 0 : table[0];
    if (count == 0 || 4 + 4 * count > table.size()) {
        khdays_log("rooms: %s's room table not read: its added rooms left out", game_path.c_str());
        return false;
    }
    // room 0's record and its model, the added rooms' pattern
    const std::size_t first = le32(table, 4);
    if (first + kRecord + 4 > table.size() || table[first + 2] == 0) {
        khdays_log("rooms: %s's room 0 has no model to copy: its added rooms left out", game_path.c_str());
        return false;
    }
    const std::size_t object = first + le32(table, first + kRecord);
    const unsigned model_member = object + 2 <= table.size() ? table[object + 1] : 0xffffffffu;
    if (model_member >= p.entries.size()) {
        khdays_log("rooms: %s's room 0's model is not in the archive: its added rooms left out", game_path.c_str());
        return false;
    }
    std::set<int> ids;
    for (unsigned r = 0; r < count; ++r)
        ids.insert(static_cast<signed char>(table[le32(table, 4 + 4 * r)]));
    const unsigned added = static_cast<unsigned>(here.size());
    // members are a byte in the records, rooms a byte in the table (the game's widths)
    if (p.entries.size() + 2 * added > 0xff || count + added > 0xff) {
        khdays_log("rooms: %s would hold more rooms or members than the game's byte counts: its added rooms left out",
                   game_path.c_str());
        return false;
    }
    // the table again: its offsets one more each, the game's records after them, the added ones after those
    std::vector<unsigned char> made(4 + 4 * (count + added), 0);
    made[0] = static_cast<unsigned char>(count + added);
    const std::size_t shift = 4 * added;
    for (unsigned r = 0; r < count; ++r)
        put32(made, 4 + 4 * r, le32(table, 4 + 4 * r) + static_cast<unsigned>(shift));
    made.insert(made.end(), table.begin() + 4 + 4 * count, table.end());
    int next_id = 0;
    for (unsigned k = 0; k < added; ++k) {
        Added &a = *here[k];
        while (ids.count(next_id) != 0)
            ++next_id;
        if (next_id > 127) {
            khdays_log("rooms: %s has no room id left for %s", game_path.c_str(), a.key.c_str());
            return false;
        }
        a.id = next_id;
        a.index = static_cast<int>(count + k);
        ids.insert(next_id);
        // its collision LZ-packed as the game's are (the room's loader unpacks what it loads)
        a.collision_member = static_cast<int>(p.entries.size());
        p.entries.push_back(khdays::assets::lz11_literals(read_all(a.collision)));
        p.descs.push_back(static_cast<unsigned>(p.entries.back().size()) | 0x80000000u);
        a.model_member = static_cast<int>(p.entries.size());
        p.entries.push_back(p.entries[model_member]);
        p.descs.push_back(p.descs[model_member]);
        made.resize((made.size() + 3) & ~std::size_t{3}, 0);
        const std::size_t at = made.size();
        put32(made, 4 + 4 * (count + k), static_cast<unsigned>(at));
        // room 0's record (what is not read kept), its own id, no exits, one object: its model
        made.insert(made.end(), table.begin() + static_cast<std::ptrdiff_t>(first),
                    table.begin() + static_cast<std::ptrdiff_t>(first + kRecord));
        made[at] = static_cast<unsigned char>(a.id);
        made[at + 1] = 0;
        made[at + 2] = 1;
        made[at + 3] = static_cast<unsigned char>(a.collision_member);
        made.resize(at + kRecord + 8, 0);
        put32(made, at + 0x1c, static_cast<unsigned>(kRecord + 4));  // its exits (none) after its objects' offsets
        put32(made, at + kRecord, static_cast<unsigned>(kRecord + 4));
        made[at + kRecord + 4] = table[object];  // the object's flags, as room 0's
        made[at + kRecord + 5] = static_cast<unsigned char>(a.model_member);
        made[at + kRecord + 6] = table[object + 2];
        made[at + kRecord + 7] = table[object + 3];
        khdays_log("rooms: %s's room %s added to %s as room %u (id %d): collision #%d, model #%d", a.mod_path.c_str(),
                   a.key.c_str(), world.c_str(), count + k, a.id, a.collision_member, a.model_member);
    }
    p.entries[0] = std::move(made);
    p.descs[0] = static_cast<unsigned>(p.entries[0].size());  // written unpacked
    out = write_p2(p);
    // KHDAYS_ROOMS_DUMP=<folder>: the archive as served, to look at
    if (const char *dump = std::getenv("KHDAYS_ROOMS_DUMP")) {
        std::ofstream file(utf8_path(dump) / utf8_path("wd_" + world), std::ios::binary);
        file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    return true;
}

extern "C" int khdays_rooms_index(const char *mod_path, const char *key, const char *world)
{
    for (const auto &a : rooms) {
        std::error_code ec;
        if (a.id >= 0 && a.key == key && a.world == world &&
            fs::equivalent(utf8_path(a.mod_path), utf8_path(mod_path), ec))
            return a.index;
    }
    return -1;
}

extern "C" int khdays_rooms_model(const char *name, char *path, int size)
{
    for (const auto &a : rooms) {
        if (a.model_member < 0)
            continue;
        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "mi/wd/wd_%s#%d/", a.world.c_str(), a.model_member);
        if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
            const std::u8string p = a.model.u8string();
            if (p.size() + 1 > static_cast<std::size_t>(size))
                return 0;
            std::memcpy(path, p.data(), p.size());
            path[p.size()] = '\0';
            return 1;
        }
    }
    return 0;
}
