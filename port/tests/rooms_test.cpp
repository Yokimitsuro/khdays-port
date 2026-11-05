/* The rooms the mods add (runtime/rooms/added.cpp) and the rooms' slots (runtime/rooms/slots.c): a synthetic world
 * archive of one room, a mod adding one, the archive as served read back -- its room table one room longer, the new
 * record (an id none of the world's, no exits, its collision and model members), its collision LZ-packed as the
 * game's, room 0's model copied -- the added room's index and model; its map's picture after the game's map pictures
 * and its entry in its world's chain; a world the mod adds (its Mission Mode slot, the game world it is played in, its
 * names); and the slots past 24 rooms. */
extern "C" {
#include "../runtime/missions/worlds.h"
}
#include "../runtime/rooms/added.h"
#include "../runtime/rooms/slots.h"

extern "C" {
#include "../host/render/png_io.h"
}

#include "khdays/assets/message.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                               \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* the mods taken at the start: one, in a temporary folder */
static std::string mod_path;
extern "C" int khdays_mods_started_count(void)
{
    return 1;
}
extern "C" const char *khdays_mods_started_path(int index)
{
    return index == 0 ? mod_path.c_str() : nullptr;
}
extern "C" void khdays_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fputc('\n', stdout);
}

static void put16(std::vector<unsigned char> &b, std::size_t at, unsigned v)
{
    b[at] = static_cast<unsigned char>(v);
    b[at + 1] = static_cast<unsigned char>(v >> 8);
}
static void put32(std::vector<unsigned char> &b, std::size_t at, unsigned v)
{
    for (int i = 0; i < 4; ++i)
        b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>(v >> (8 * i));
}
static unsigned le16(const std::vector<unsigned char> &b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8);
}
static unsigned le32(const std::vector<unsigned char> &b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (unsigned(b[at + 3]) << 24);
}

/* A P2 of `entries` (each one's bytes and descriptor), its tables then each on a sector. */
static std::vector<unsigned char> p2(const std::vector<std::pair<std::vector<unsigned char>, unsigned>> &entries)
{
    const unsigned n = static_cast<unsigned>(entries.size());
    const std::size_t sizes = 0x10 + ((n + 1) / 2) * 4;
    std::vector<unsigned char> b(0x200, 0);
    b[0] = 'P';
    b[1] = '2';
    put16(b, 2, n);
    put32(b, 0xc, 0x200);
    for (unsigned e = 0; e < n; ++e) {
        put16(b, 0x10 + 2 * e, static_cast<unsigned>((b.size() - 0x200) >> 9));
        put32(b, sizes + 4 * e, entries[e].second);
        b.insert(b.end(), entries[e].first.begin(), entries[e].first.end());
        b.resize((b.size() + 0x1ff) & ~std::size_t{0x1ff}, 0);
    }
    return b;
}

/* An entry of a P2 as stored, LZ-unpacked when its descriptor says so. */
static std::vector<unsigned char> entry(const std::vector<unsigned char> &b, unsigned e, unsigned *desc)
{
    const unsigned n = le16(b, 2) & 0x1ff, base = le32(b, 0xc);
    const std::size_t sizes = 0x10 + ((n + 1) / 2) * 4;
    *desc = le32(b, sizes + 4 * e);
    const std::size_t at = base + (static_cast<std::size_t>(le16(b, 0x10 + 2 * e)) << 9);
    std::vector<unsigned char> raw(b.begin() + static_cast<std::ptrdiff_t>(at),
                                   b.begin() + static_cast<std::ptrdiff_t>(at + (*desc & 0x7fffffff)));
    return (*desc & 0x80000000u) ? khdays::assets::lz_decompress(raw) : raw;
}

static void a_mods_room_added_to_a_world(void)
{
    const fs::path mod = fs::temp_directory_path() / "khdays_rooms_test";
    std::error_code ec;
    fs::remove_all(mod, ec);
    fs::create_directories(mod / "rooms" / "eh" / "plaza");
    mod_path = mod.string();
    std::ofstream(mod / "rooms" / "rooms.txt") << "# a room\nplaza eh\nnot-a-world\ncove isle\ncave nowhere\n";
    // a world of its own played in he, and one with no game world (left out)
    std::ofstream(mod / "rooms" / "worlds.txt")
        << "# its worlds\n[world isle]\nlike = he\nname en = Isle\nname es = Isla del sol\n"
           "[world nowhere]\nname = None\n";
    fs::create_directories(mod / "rooms" / "isle" / "cove");
    std::ofstream(mod / "rooms" / "isle" / "cove" / "room.gltf") << "{}";
    std::ofstream(mod / "rooms" / "isle" / "cove" / "collision.bin") << "c";
    std::ofstream(mod / "rooms" / "eh" / "plaza" / "room.gltf") << "{}";
    const std::vector<unsigned char> collision(0x123, 0x5a);
    std::ofstream(mod / "rooms" / "eh" / "plaza" / "collision.bin", std::ios::binary)
        .write(reinterpret_cast<const char *>(collision.data()), static_cast<std::streamsize>(collision.size()));
    // its map: see-through but its first two pixels (a floor, an edge)
    std::vector<unsigned char> map(192 * 160, 15);
    map[0] = 14;
    map[1] = 10;
    std::vector<unsigned char> greys(16 * 3, 128);
    CHECK(khdays_png_write_indexed((mod / "rooms" / "eh" / "plaza" / "map.png").string().c_str(), map.data(), 192, 160,
                                   greys.data(), 16, 0) == nullptr);
    std::ofstream(mod / "rooms" / "eh" / "plaza" / "map.txt")
        << "# its map\norigin 60 40\nsize 15 10\ncell 0.4\nname Plaza\nname.es Plaza mayor\n";

    // the world: its room table (one room, id 0: its collision member 2, its model member 3), packed
    std::vector<unsigned char> table(8 + 0x28, 0);
    table[0] = 1;
    put32(table, 4, 8);
    table[8] = 0;                   // id
    table[9] = 0;                   // exits
    table[10] = 1;                  // objects
    table[11] = 2;                  // collision
    put32(table, 8 + 0x14, 0x1234); // a word not read, kept
    put32(table, 8 + 0x1c, 0x24);
    put32(table, 8 + 0x20, 0x24);
    table[8 + 0x24] = 0x80;
    table[8 + 0x25] = 3;
    const auto packed = khdays::assets::lz11_literals(table);
    const std::vector<unsigned char> model{'K', 'A', 'P', 'H', 1, 2, 3, 4};
    const auto world = p2({{packed, static_cast<unsigned>(packed.size()) | 0x80000000u},
                           {{}, 0},
                           {std::vector<unsigned char>(16, 7), 16},
                           {model, static_cast<unsigned>(model.size())}});

    khdays_rooms_load();
    std::vector<unsigned char> out;
    CHECK(!khdays_rooms_merge("mi/wd/wd_tt", world, out)); /* no room added to tt */
    CHECK(!khdays_rooms_merge("mi/mi/0001", world, out));
    CHECK(khdays_rooms_merge("mi/wd/wd_eh", world, out));
    CHECK((le16(out, 2) & 0x1ff) == 6);
    unsigned desc = 0;
    const auto rooms = entry(out, 0, &desc);
    CHECK((desc & 0x80000000u) == 0 && rooms[0] == 2);
    // room 0 where it was, its offset moved past the longer table
    CHECK(le32(rooms, 4) == 12 && rooms[12] == 0 && rooms[15] == 2);
    const std::size_t added = le32(rooms, 8);
    CHECK(rooms[added] == 1 && rooms[added + 1] == 0 && rooms[added + 2] == 1 && rooms[added + 3] == 4);
    CHECK(le32(rooms, added + 0x14) == 0x1234);
    CHECK(rooms[added + le32(rooms, added + 0x20)] == 0x80 && rooms[added + le32(rooms, added + 0x20) + 1] == 5);
    // its collision packed, room 0's model copied
    const auto c = entry(out, 4, &desc);
    CHECK((desc & 0x80000000u) != 0 && c == collision);
    CHECK(entry(out, 5, &desc) == model);
    CHECK(khdays_rooms_index(mod_path.c_str(), "plaza", "eh") == 1);
    CHECK(khdays_rooms_index(mod_path.c_str(), "plaza", "tt") == -1);
    char path[512];
    CHECK(khdays_rooms_model("mi/wd/wd_eh#5/mdl/eh_00", path, sizeof(path)) &&
          std::strstr(path, "room.gltf") != nullptr);
    CHECK(!khdays_rooms_model("mi/wd/wd_eh#3/mdl/eh_00", path, sizeof(path)));

    // its map: an NCGR as the game's (20 x 24 tiles of 4 bits), the picture after it
    std::vector<unsigned char> ncgr(0x30 + 0x3c00 + 0x10, 0xff);
    std::memcpy(ncgr.data(), "RGCN", 4);
    std::memcpy(ncgr.data() + 0x10, "RAHC", 4);
    put16(ncgr, 0x18, 20);
    put16(ncgr, 0x1a, 24);
    put32(ncgr, 0x1c, 3);
    put32(ncgr, 0x28, 0x3c00);
    put32(ncgr, 0x2c, 0x18);
    const auto pictures = p2({{ncgr, static_cast<unsigned>(ncgr.size())}});
    KhdaysGameFiles game;
    game.read = [&](const std::string &file) {
        return file == "UI/btl/mapchr.p2" ? pictures : file == "mi/wd/wd_eh" ? world : std::vector<unsigned char>();
    };
    CHECK(khdays_rooms_serves("UI/btl/mapchr.p2") && khdays_rooms_serves("UI/btl/es/map.p2") &&
          khdays_rooms_serves("mi/wd/wd_eh") && !khdays_rooms_serves("UI/btl/es/mapx.p2"));
    CHECK(khdays_rooms_merge("UI/btl/mapchr.p2", pictures, out, game));
    CHECK((le16(out, 2) & 0x1ff) == 2);
    const auto picture = entry(out, 1, &desc);
    CHECK(picture.size() == ncgr.size() && picture[0x30] == (14 | (10 << 4)) && picture[0x31] == 0xff &&
          picture[0x30 + 0x3c00] == 0xff);
    // its world's chain (eh's, the game's fifth): one entry of the game's (id 7, "A"), ended by -1
    std::vector<unsigned char> chain(0x1c + 4, 0);
    put32(chain, 0, 0x1c);
    put16(chain, 4, 0x18);
    put16(chain, 6, 7);
    chain[0x18] = 'A';
    put32(chain, 0x1c, 0xffffffffu);
    const auto packed_chain = khdays::assets::lz11_literals(chain);
    const std::vector<unsigned char> none{0xff, 0xff, 0xff, 0xff};
    const auto maps = p2({{none, 4},
                          {none, 4},
                          {none, 4},
                          {none, 4},
                          {packed_chain, static_cast<unsigned>(packed_chain.size()) | 0x80000000u}});
    CHECK(khdays_rooms_merge("UI/btl/es/map.p2", maps, out, game));
    const auto merged = entry(out, 4, &desc);
    const std::size_t at = le32(merged, 0);
    CHECK((desc & 0x80000000u) == 0 && at == 0x1c);
    // after the game's: its id, where its origin falls, the part held, its scale, its picture, its Spanish name
    CHECK(le16(merged, at + 6) == 1 && le16(merged, at + 8) == 60 && le16(merged, at + 0xa) == 40 &&
          le16(merged, at + 0xc) == 15 && le16(merged, at + 0xe) == 10 && le32(merged, at + 0x10) == 1638 &&
          le32(merged, at + 0x14) == 1);
    const char *spanish = "Plaza mayor";
    for (std::size_t i = 0; i <= std::strlen(spanish); ++i)
        CHECK(le16(merged, at + 0x18 + 2 * i) == static_cast<unsigned char>(spanish[i]));
    CHECK(le32(merged, at + le32(merged, at)) == 0xffffffffu);
    CHECK(entry(out, 1, &desc) == none);
    CHECK(khdays_rooms_merge("UI/btl/en/map.p2", maps, out, game));
    CHECK(le16(entry(out, 4, &desc), at + 0x18) == 'P');

    // its world: Mission Mode's slot 13, past the game's twelve and their thirteenth name, shown at he's place
    char like[3] = "";
    CHECK(khdays_mission_slots() == 14);
    CHECK(khdays_mod_world(mod_path.c_str(), "isle", like) == 13 && std::strcmp(like, "he") == 0);
    CHECK(khdays_mod_world(mod_path.c_str(), "nowhere", like) == -1);
    CHECK(khdays_mission_slot_shown(13) == khdays_world_index("he") && khdays_mission_slot_shown(5) == 5);
    // its name after the game's thirteen, in each language (English where it has none)
    std::vector<unsigned char> names(8, 0);
    put32(names, 0, 8);
    put32(names, 4, 13);
    for (int i = 0; i < 13; ++i) {
        const std::size_t at_name = names.size();
        names.resize(at_name + 8, 0);
        put32(names, at_name, 8);
        names[at_name + 4] = static_cast<unsigned char>('A' + i);
    }
    const auto packed_names = khdays::assets::lz11_literals(names);
    CHECK(khdays_rooms_serves("UI/cm/str/world_id_es.s.z") && !khdays_rooms_serves("UI/cm/str/world_id_es.s"));
    CHECK(khdays_rooms_merge("UI/cm/str/world_id_es.s.z", packed_names, out, game));
    auto served = khdays::assets::lz_decompress(out);
    CHECK(le32(served, 4) == 14 && served.size() == names.size() + 4 + 2 * (std::strlen("Isla del sol") + 1));
    CHECK(le16(served, names.size() + 4) == 'I' && le16(served, names.size() + 4 + 2 * 5) == 'd');
    CHECK(khdays_rooms_merge("UI/cm/str/world_id_de.s.z", packed_names, out, game));
    served = khdays::assets::lz_decompress(out);
    CHECK(le32(served, names.size()) == 4 + 2 * (std::strlen("Isle") + 1));
    fs::remove_all(mod, ec);
}

static void slots_past_24_rooms(void)
{
    CHECK(khdays_room_slot(30) == -1);
    khdays_room_slot_set(30, 2);
    khdays_room_slot_set(3, 1);
    CHECK(khdays_room_slot(30) == 2 && khdays_room_slot(3) == 1 && khdays_room_slot(29) == -1);
    khdays_room_slot_set(30, -1);
    CHECK(khdays_room_slot(30) == -1);
    khdays_room_slots_clear();
    CHECK(khdays_room_slot(3) == -1 && khdays_room_slot(-1) == -1);
}

int main(void)
{
    a_mods_room_added_to_a_world();
    slots_past_24_rooms();
    if (failures != 0) {
        std::fprintf(stderr, "rooms test: %d failed\n", failures);
        return 1;
    }
    std::printf("rooms test passed\n");
    return 0;
}
