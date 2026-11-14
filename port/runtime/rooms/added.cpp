/* The rooms the mods add to the game's mission worlds (added.h). */
#include "added.h"

#include "../core/session_log.h"
extern "C" {
#include "../missions/worlds.h"
}
#include "../mods/list.h"

#include "khdays/assets/message.h" // lz_decompress, lz11_literals

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

extern "C" {
#include "../../host/render/png_io.h"
}

namespace fs = std::filesystem;

namespace {

// The game's map pictures (UI/btl/mapchr.p2): 24 x 20 tiles of 8 x 8 pixels, 4 bits a pixel.
constexpr int kMapWidth = 192, kMapHeight = 160;
constexpr unsigned kMapNone = 15; // the map's see-through colour

struct Added {
    std::string mod_path, key, world;
    fs::path folder, model, collision; // rooms/<world as rooms.txt names it>/<key>/ and its files
    int id = -1, index = -1; // given when its world is planned: its id (none of the world's), its place
    int collision_member = -1, model_member = -1;
    // its map on the lower screen (map.png, map.txt), when it has one: the picture's colours, where the room's origin
    // falls in it, the part it holds (tiles), the game units a pixel spans (20.12), its names ("" all languages')
    std::vector<unsigned char> map;
    int origin_x = 0, origin_y = 0, map_width = 0, map_height = 0, cell = 0;
    std::map<std::string, std::string> names;
    int picture = -1; // its picture's member of UI/btl/mapchr.p2
};

// A world a mod adds (rooms/worlds.txt): played in a game world's archive and resources (`like`: its rooms are added
// to that world's), listed in Mission Mode under its own name in a slot past the game's.
struct ModWorld {
    std::string mod_path, key, like;
    std::map<std::string, std::string> names; // by language, "" all languages'
    int slot = -1;
};

// Mission Mode's world slots (Ov008_StepCursorToAcceptedSlot's twelve), and the first a mod world takes: past the
// game's world names (UI/cm/str/world_id_<language>.s.z: the twelve worlds' and a thirteenth).
constexpr int kGameSlots = 12, kFirstModSlot = 13;
constexpr int kMostSlots = 0xff; // the list's slot is a byte (its +0x54)

std::vector<Added> rooms;
std::vector<ModWorld> worlds;
bool loaded = false;
std::map<std::string, bool> planned; // by world: whether its added rooms can be put in
bool pictures_numbered = false;

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
    unsigned flags = 0; // the count's word above its 9 bits
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

// An entry's bytes as the game reads them (unpacked when its descriptor says it is packed); empty when it is not read.
std::vector<unsigned char> unpacked(const P2 &p, std::size_t e)
{
    if ((p.descs[e] & 0x80000000u) == 0 || p.entries[e].empty())
        return p.entries[e];
    try {
        return khdays::assets::lz_decompress(p.entries[e]);
    } catch (const std::exception &) {
        return {};
    }
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

// The worlds the mod in `dir` adds (rooms/worlds.txt): blocks "[world <key>]" of "like = <a game world's code>",
// "name = <text>" and "name <language> = <text>".
void read_worlds(const fs::path &dir, const char *mod_path)
{
    std::ifstream list(dir / "rooms" / "worlds.txt", std::ios::binary);
    std::string line;
    int number = 0;
    ModWorld *open = nullptr;
    const auto close = [&]() {
        if (open != nullptr && khdays_world_index(open->like.c_str()) < 0) {
            khdays_log("rooms: %s/rooms/worlds.txt: world %s has no game world to be played in (like = tt ...): left "
                       "out",
                       mod_path, open->key.c_str());
            worlds.pop_back();
        }
        open = nullptr;
    };
    while (std::getline(list, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = line.substr(0, line.find('#'));
        std::istringstream words(line);
        std::string first;
        if (!(words >> first))
            continue;
        if (first == "[world") {
            close();
            std::string key;
            words >> key;
            if (!key.empty() && key.back() == ']')
                key.pop_back();
            if (key.empty() || khdays_world_code_taken(key.c_str())) {
                khdays_log("rooms: %s/rooms/worlds.txt line %d: a world's key, none of the game's codes: [world <key>]",
                           mod_path, number);
                continue;
            }
            worlds.push_back({mod_path, key, "", {}, -1});
            open = &worlds.back();
            continue;
        }
        if (open == nullptr)
            continue;
        const auto equals = line.find('=');
        std::string value = equals == std::string::npos ? std::string() : line.substr(equals + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        std::string language;
        if (first == "like") {
            open->like = value;
        } else if (first == "name" && (words >> language) && language != "=") {
            open->names[language] = value;
        } else if (first == "name") {
            open->names[""] = value;
        }
    }
    close();
}

// Its map on the lower screen (map.png in the map's colours, map.txt: "origin x y", "size w h" in tiles, "cell" the
// game units a pixel spans, "name" and "name.<language>"), when the folder has one.
void read_map(const fs::path &folder, Added *a)
{
    std::error_code ec;
    if (!fs::is_regular_file(folder / "map.png", ec))
        return;
    std::ifstream text(folder / "map.txt", std::ios::binary);
    std::string line;
    bool origin = false, size = false;
    double cell = 0;
    while (std::getline(text, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream words(line);
        std::string what;
        words >> what;
        if (what == "origin") {
            origin = static_cast<bool>(words >> a->origin_x >> a->origin_y);
        } else if (what == "size") {
            size = static_cast<bool>(words >> a->map_width >> a->map_height);
        } else if (what == "cell") {
            words >> cell;
        } else if (what == "name" || what.rfind("name.", 0) == 0) {
            std::string name;
            std::getline(words >> std::ws, name);
            a->names[what == "name" ? std::string() : what.substr(5)] = name;
        }
    }
    if (!origin || !size || cell <= 0 || a->origin_x < 0 || a->origin_y < 0 || a->map_width <= 0 ||
        a->map_height <= 0) {
        khdays_log("rooms: %s's room %s: its map.txt does not say its origin, size and cell: no map",
                   a->mod_path.c_str(), a->key.c_str());
        return;
    }
    a->cell = static_cast<int>(cell * 4096 + 0.5);
    const std::u8string png = (folder / "map.png").u8string();
    int w = 0, h = 0;
    const char *error = nullptr;
    uint8_t *indices = khdays_png_read_indexed(reinterpret_cast<const char *>(png.c_str()), &w, &h, nullptr, &error);
    if (indices == nullptr) {
        khdays_log("rooms: %s's room %s: map.png not read (%s): no map", a->mod_path.c_str(), a->key.c_str(),
                   error ? error : "?");
        return;
    }
    if (w != kMapWidth || h != kMapHeight)
        khdays_log("rooms: %s's room %s: map.png is %dx%d, the game's maps %dx%d: put in its top left",
                   a->mod_path.c_str(), a->key.c_str(), w, h, kMapWidth, kMapHeight);
    a->map.assign(static_cast<std::size_t>(kMapWidth) * kMapHeight, static_cast<unsigned char>(kMapNone));
    for (int y = 0; y < std::min(h, kMapHeight); ++y)
        for (int x = 0; x < std::min(w, kMapWidth); ++x)
            a->map[static_cast<std::size_t>(y) * kMapWidth + static_cast<std::size_t>(x)] =
                indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] & 15;
    khdays_png_free(indices);
}

// The world's added rooms given their ids, places and members from its archive as the game has it (`archive`); false
// (said once) when they cannot be put in. Once a world: the archive's merge and the maps' agree.
bool plan(const std::string &world, const std::vector<unsigned char> &archive)
{
    if (const auto done = planned.find(world); done != planned.end())
        return done->second;
    std::vector<Added *> here;
    for (auto &a : rooms) {
        if (a.world == world)
            here.push_back(&a);
    }
    if (here.empty())
        return false;
    bool &ok = planned[world];
    ok = false;
    const std::string game_path = "mi/wd/wd_" + world;
    P2 p;
    if (!read_p2(archive, &p) || p.entries.size() < 2) {
        khdays_log("rooms: %s is no world archive the port reads: its added rooms left out", game_path.c_str());
        return false;
    }
    const std::vector<unsigned char> table = unpacked(p, 0);
    const unsigned count = table.empty() ? 0 : table[0];
    if (count == 0 || 4 + 4 * count > table.size()) {
        khdays_log("rooms: %s's room table not read: its added rooms left out", game_path.c_str());
        return false;
    }
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
    const unsigned added = static_cast<unsigned>(here.size());
    // members are a byte in the records, rooms a byte in the table (the game's widths)
    if (p.entries.size() + 2 * added > 0xff || count + added > 0xff) {
        khdays_log("rooms: %s would hold more rooms or members than the game's byte counts: its added rooms left out",
                   game_path.c_str());
        return false;
    }
    std::set<int> ids;
    for (unsigned r = 0; r < count; ++r)
        ids.insert(static_cast<signed char>(table[le32(table, 4 + 4 * r)]));
    int next_id = 0;
    for (unsigned k = 0; k < added; ++k) {
        while (ids.count(next_id) != 0)
            ++next_id;
        if (next_id > 127) {
            khdays_log("rooms: %s has no room id left for %s: its added rooms left out", game_path.c_str(),
                       here[k]->key.c_str());
            for (Added *a : here)
                a->id = -1;
            return false;
        }
        here[k]->id = next_id;
        ids.insert(next_id);
        here[k]->index = static_cast<int>(count + k);
        here[k]->collision_member = static_cast<int>(p.entries.size() + 2 * k);
        here[k]->model_member = static_cast<int>(p.entries.size() + 2 * k + 1);
    }
    ok = true;
    return true;
}

// The added maps' pictures numbered after the game's `count` (UI/btl/mapchr.p2's), in the rooms' order: no more than
// the archive's count can say (nine bits).
void number_pictures(unsigned count)
{
    if (pictures_numbered)
        return;
    pictures_numbered = true;
    unsigned next = count;
    for (auto &a : rooms) {
        if (a.map.empty())
            continue;
        if (next > 0x1ff) {
            khdays_log(
                "rooms: UI/btl/mapchr.p2 holds as many pictures as its count can say: %s's room %s's map left out",
                a.mod_path.c_str(), a.key.c_str());
            continue;
        }
        a.picture = static_cast<int>(next++);
    }
}

// A UTF-8 name as the maps' chains hold names: UTF-16, two bytes a unit, ended by 0.
std::vector<unsigned char> utf16(const std::string &s)
{
    std::vector<unsigned char> out;
    auto unit = [&](unsigned u) {
        out.push_back(static_cast<unsigned char>(u));
        out.push_back(static_cast<unsigned char>(u >> 8));
    };
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int more = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        unsigned v = more == 0 ? c : c & (0x3fu >> more);
        for (int k = 1; k <= more && i + static_cast<std::size_t>(k) < s.size(); ++k)
            v = (v << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3fu);
        i += static_cast<std::size_t>(more) + 1;
        if (v >= 0x10000) {
            v -= 0x10000;
            unit(0xd800 | (v >> 10));
            unit(0xdc00 | (v & 0x3ff));
        } else {
            unit(v);
        }
    }
    unit(0);
    return out;
}

// UI/btl/mapchr.p2 with the added maps' pictures after the game's: each an NCGR as the game's (one of them the
// pattern, its pixels the map's: 8 x 8 tiles by rows, two pixels a byte, the left one low).
bool merge_pictures(const std::vector<unsigned char> &source, std::vector<unsigned char> &out)
{
    P2 p;
    if (!read_p2(source, &p))
        return false;
    number_pictures(static_cast<unsigned>(p.entries.size()));
    std::vector<unsigned char> pattern;
    std::size_t pixels = 0;
    for (std::size_t e = 0; e < p.entries.size() && pattern.empty(); ++e) {
        const auto b = unpacked(p, e);
        // "RGCN", its "RAHC": 20 x 24 tiles of 4 bits, its pixels' bytes and where they start
        if (b.size() < 0x30 || std::memcmp(b.data(), "RGCN", 4) != 0 || std::memcmp(b.data() + 0x10, "RAHC", 4) != 0 ||
            le16(b, 0x18) != kMapHeight / 8 || le16(b, 0x1a) != kMapWidth / 8 || le32(b, 0x1c) != 3)
            continue;
        const std::size_t at = 0x18 + le32(b, 0x2c), bytes = le32(b, 0x28);
        if (bytes != kMapWidth * kMapHeight / 2 || at + bytes > b.size())
            continue;
        pattern = b;
        pixels = at;
    }
    bool any = false;
    for (const auto &a : rooms) {
        if (a.picture < 0)
            continue;
        if (pattern.empty()) {
            khdays_log("rooms: UI/btl/mapchr.p2 holds no map picture to follow: the added rooms' maps left out");
            return false;
        }
        if (a.picture != static_cast<int>(p.entries.size()))
            continue; // (numbered from another count: a file not the game's)
        std::vector<unsigned char> made = pattern;
        for (int tile = 0; tile < (kMapWidth / 8) * (kMapHeight / 8); ++tile) {
            const int tx = tile % (kMapWidth / 8), ty = tile / (kMapWidth / 8);
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; col += 2) {
                    const auto px = [&](int c) {
                        return a.map[static_cast<std::size_t>(ty * 8 + row) * kMapWidth +
                                     static_cast<std::size_t>(tx * 8 + c)];
                    };
                    made[pixels + static_cast<std::size_t>(tile) * 32 + static_cast<std::size_t>(row) * 4 +
                         static_cast<std::size_t>(col / 2)] = static_cast<unsigned char>(px(col) | (px(col + 1) << 4));
                }
        }
        p.entries.push_back(std::move(made));
        p.descs.push_back(static_cast<unsigned>(p.entries.back().size()));
        any = true;
    }
    if (any)
        out = write_p2(p);
    return any;
}

// UI/btl/<language>/map.p2 with an entry for each added room that has a map in its world's chain (looked up by the
// room's id): u32 its length, u16 where its name starts, u16 the room's id, u16 x2 where the room's origin falls in its
// picture, u16 x2 the part held (tiles), u32 the game units a pixel spans (20.12), u32 its picture's member, its name
// (UTF-16); the chain ended by a length of -1.
bool merge_chains(const std::string &language, const std::vector<unsigned char> &source,
                  std::vector<unsigned char> &out, const KhdaysGameFiles &game)
{
    bool wanted = false;
    for (const auto &a : rooms)
        wanted = wanted || !a.map.empty();
    if (!wanted || !game.read)
        return false;
    P2 p;
    if (!read_p2(source, &p))
        return false;
    {
        P2 pictures;
        if (!read_p2(game.read("UI/btl/mapchr.p2"), &pictures))
            return false;
        number_pictures(static_cast<unsigned>(pictures.entries.size()));
    }
    bool any = false;
    for (auto &a : rooms) {
        if (a.picture < 0 || !plan(a.world, game.read("mi/wd/wd_" + a.world)) || a.id < 0)
            continue;
        const int member = khdays_world_map_chain(khdays_world_index(a.world.c_str()));
        if (member < 0 || static_cast<std::size_t>(member) >= p.entries.size()) {
            khdays_log("rooms: world %s has no map chain in UI/btl/%s/map.p2: %s's room %s's map left out",
                       a.world.c_str(), language.c_str(), a.mod_path.c_str(), a.key.c_str());
            continue;
        }
        std::vector<unsigned char> chain = unpacked(p, static_cast<std::size_t>(member));
        // its end: the length -1
        std::size_t end = 0;
        while (end + 4 <= chain.size() && le32(chain, end) != 0xffffffffu && le32(chain, end) != 0)
            end += le32(chain, end);
        if (end + 4 > chain.size())
            continue;
        const auto named = a.names.find(language);
        const auto fallback = a.names.find("");
        const auto name = utf16(named != a.names.end()      ? named->second
                                : fallback != a.names.end() ? fallback->second
                                                            : a.key);
        std::vector<unsigned char> entry(0x18, 0);
        entry.insert(entry.end(), name.begin(), name.end());
        entry.resize((entry.size() + 3) & ~std::size_t{3}, 0);
        put32(entry, 0, static_cast<unsigned>(entry.size()));
        put16(entry, 4, 0x18);
        put16(entry, 6, static_cast<unsigned>(a.id));
        put16(entry, 8, static_cast<unsigned>(a.origin_x));
        put16(entry, 0xa, static_cast<unsigned>(a.origin_y));
        put16(entry, 0xc, static_cast<unsigned>(a.map_width));
        put16(entry, 0xe, static_cast<unsigned>(a.map_height));
        put32(entry, 0x10, static_cast<unsigned>(a.cell));
        put32(entry, 0x14, static_cast<unsigned>(a.picture));
        chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(end), entry.begin(), entry.end());
        const std::size_t m = static_cast<std::size_t>(member);
        p.entries[m] = std::move(chain);
        p.descs[m] = static_cast<unsigned>(p.entries[m].size());
        any = true;
    }
    if (any)
        out = write_p2(p);
    return any;
}

// UI/cm/str/world_id_<language>.s.z with the mods' worlds' names past the game's: u32 8, u32 the count, each name a
// u32 its length (these four bytes with it) and its UTF-16 text ended by 0; packed as the game's (.z).
bool merge_world_names(const std::string &language, const std::vector<unsigned char> &source,
                       std::vector<unsigned char> &out)
{
    bool wanted = false;
    for (const auto &w : worlds)
        wanted = wanted || w.slot >= 0;
    if (!wanted)
        return false;
    std::vector<unsigned char> text;
    try {
        text = khdays::assets::lz_decompress(source);
    } catch (const std::exception &) {
        text.clear();
    }
    if (text.size() < 8 || le32(text, 0) != 8) {
        khdays_log("rooms: UI/cm/str/world_id_%s.s.z is no list of names the port reads: the mods' worlds unnamed",
                   language.c_str());
        return false;
    }
    unsigned count = le32(text, 4);
    std::size_t end = 8;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned n = le32(text, end);
        if (n < 4 || end + n > text.size())
            return false;
        end += n;
    }
    text.resize(end);
    const auto add = [&](const std::string &name) {
        const auto units = utf16(name);
        const std::size_t at = text.size();
        text.resize(at + 4);
        put32(text, at, static_cast<unsigned>(units.size() + 4));
        text.insert(text.end(), units.begin(), units.end());
        ++count;
    };
    for (const auto &w : worlds) {
        if (w.slot < 0)
            continue;
        while (static_cast<int>(count) < w.slot)
            add(""); // a slot no world takes
        const auto named = w.names.find(language);
        const auto all = w.names.find("");
        const auto english = w.names.find("en");
        add(named != w.names.end()     ? named->second
            : all != w.names.end()     ? all->second
            : english != w.names.end() ? english->second
                                       : w.key);
    }
    put32(text, 4, count);
    out = khdays::assets::lz11_literals(text);
    return true;
}

} // namespace

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
        read_worlds(dir, path);
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
            if (!(words >> a.world)) {
                khdays_log("rooms: %s/rooms/rooms.txt line %d: not <key> <world>", path, number);
                continue;
            }
            // its folder by the world it names; a world the mod adds plays in its game world's archive
            const fs::path folder = dir / "rooms" / utf8_path(a.world) / utf8_path(a.key);
            if (khdays_world_index(a.world.c_str()) < 0) {
                const auto own = std::find_if(worlds.begin(), worlds.end(), [&](const ModWorld &w) {
                    return w.mod_path == path && w.key == a.world;
                });
                if (own == worlds.end()) {
                    khdays_log("rooms: %s/rooms/rooms.txt line %d: %s is no game world nor one of the mod's", path,
                               number, a.world.c_str());
                    continue;
                }
                a.world = own->like;
            }
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
            a.folder = folder;
            read_map(folder, &a);
            rooms.push_back(std::move(a));
        }
    }
    int slot = kFirstModSlot;
    for (auto &w : worlds) {
        if (slot > kMostSlots) {
            khdays_log("rooms: %s's world %s: Mission Mode's slots end at %d (a byte): left out of its list",
                       w.mod_path.c_str(), w.key.c_str(), kMostSlots);
            continue;
        }
        w.slot = slot++;
        khdays_log("rooms: %s's world %s, played in %s, is Mission Mode's slot %d", w.mod_path.c_str(), w.key.c_str(),
                   w.like.c_str(), w.slot);
    }
    if (!rooms.empty())
        khdays_log("rooms: %zu rooms added by the mods", rooms.size());
}

extern "C" int khdays_mission_slots(void)
{
    khdays_rooms_load();
    int last = -1;
    for (const auto &w : worlds)
        last = std::max(last, w.slot);
    return last < 0 ? kGameSlots : last + 1;
}

extern "C" int khdays_mod_world(const char *mod_path, const char *key, char like[3])
{
    khdays_rooms_load();
    for (const auto &w : worlds) {
        std::error_code ec;
        if (w.slot >= 0 && w.key == key && fs::equivalent(utf8_path(w.mod_path), utf8_path(mod_path), ec)) {
            std::snprintf(like, 3, "%s", w.like.c_str());
            return w.slot;
        }
    }
    return -1;
}

extern "C" int khdays_mission_slot_shown(int slot)
{
    for (const auto &w : worlds)
        if (w.slot == slot)
            return khdays_world_index(w.like.c_str());
    return slot;
}

bool khdays_rooms_serves(const std::string &game_path)
{
    const std::string chain = "/map.p2";
    const std::string names = "UI/cm/str/world_id_";
    if (game_path.rfind(names, 0) == 0 && game_path.size() == names.size() + 6 &&
        game_path.compare(game_path.size() - 4, 4, ".s.z") == 0)
        return true;
    return game_path.rfind("mi/wd/wd_", 0) == 0 || game_path == "UI/btl/mapchr.p2" ||
           (game_path.rfind("UI/btl/", 0) == 0 && game_path.size() > 7 + chain.size() &&
            game_path.compare(game_path.size() - chain.size(), chain.size(), chain) == 0);
}

bool khdays_rooms_merge(const std::string &game_path, const std::vector<unsigned char> &source,
                        std::vector<unsigned char> &out, const KhdaysGameFiles &game)
{
    if (game_path == "UI/btl/mapchr.p2")
        return merge_pictures(source, out);
    if (game_path.rfind("UI/cm/str/world_id_", 0) == 0 && khdays_rooms_serves(game_path))
        return merge_world_names(game_path.substr(19, 2), source, out);
    if (game_path.rfind("UI/btl/", 0) == 0 && khdays_rooms_serves(game_path))
        return merge_chains(game_path.substr(7, game_path.size() - 7 - 7), source, out, game);
    if (game_path.rfind("mi/wd/wd_", 0) != 0)
        return false;
    const std::string world = game_path.substr(9);
    if (!plan(world, source))
        return false;
    std::vector<Added *> here;
    for (auto &a : rooms) {
        if (a.world == world)
            here.push_back(&a);
    }
    P2 p;
    read_p2(source, &p);
    const std::vector<unsigned char> table = unpacked(p, 0);
    const unsigned count = table[0];
    const std::size_t first = le32(table, 4);
    const std::size_t object = first + le32(table, first + kRecord);
    const unsigned model_member = table[object + 1];
    const unsigned added = static_cast<unsigned>(here.size());
    // the table again: its offsets one more each, the game's records after them, the added ones after those
    std::vector<unsigned char> made(4 + 4 * (count + added), 0);
    made[0] = static_cast<unsigned char>(count + added);
    const std::size_t shift = 4 * added;
    for (unsigned r = 0; r < count; ++r)
        put32(made, 4 + 4 * r, le32(table, 4 + 4 * r) + static_cast<unsigned>(shift));
    made.insert(made.end(), table.begin() + 4 + 4 * count, table.end());
    for (unsigned k = 0; k < added; ++k) {
        Added &a = *here[k];
        // its collision LZ-packed as the game's are (the room's loader unpacks what it loads)
        p.entries.push_back(khdays::assets::lz11_literals(read_all(a.collision)));
        p.descs.push_back(static_cast<unsigned>(p.entries.back().size()) | 0x80000000u);
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
        put32(made, at + 0x1c, static_cast<unsigned>(kRecord + 4)); // its exits (none) after its objects' offsets
        put32(made, at + kRecord, static_cast<unsigned>(kRecord + 4));
        made[at + kRecord + 4] = table[object]; // the object's flags, as room 0's
        made[at + kRecord + 5] = static_cast<unsigned char>(a.model_member);
        made[at + kRecord + 6] = table[object + 2];
        made[at + kRecord + 7] = table[object + 3];
        khdays_log("rooms: %s's room %s added to %s as room %u (id %d): collision #%d, model #%d", a.mod_path.c_str(),
                   a.key.c_str(), world.c_str(), count + k, a.id, a.collision_member, a.model_member);
    }
    p.entries[0] = std::move(made);
    p.descs[0] = static_cast<unsigned>(p.entries[0].size()); // written unpacked
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

extern "C" int khdays_rooms_folder(const char *world, int index, char *folder, int size)
{
    for (const auto &a : rooms) {
        if (a.id < 0 || a.index != index || a.world != world)
            continue;
        const std::u8string p = a.folder.u8string();
        if (p.size() + 1 > static_cast<std::size_t>(size))
            return 0;
        std::memcpy(folder, p.data(), p.size());
        folder[p.size()] = '\0';
        return 1;
    }
    return 0;
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
