/* The rooms the mods add (runtime/rooms/added.cpp) and the rooms' slots (runtime/rooms/slots.c): a synthetic world
 * archive of one room, a mod adding one, the archive as served read back -- its room table one room longer, the new
 * record (an id none of the world's, no exits, its collision and model members), its collision LZ-packed as the
 * game's, room 0's model copied -- the added room's index and model; and the slots past 24 rooms. */
#include "../runtime/rooms/added.h"
#include "../runtime/rooms/slots.h"

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
    std::ofstream(mod / "rooms" / "rooms.txt") << "# a room\nplaza eh\nnot-a-world\n";
    std::ofstream(mod / "rooms" / "eh" / "plaza" / "room.gltf") << "{}";
    const std::vector<unsigned char> collision(0x123, 0x5a);
    std::ofstream(mod / "rooms" / "eh" / "plaza" / "collision.bin", std::ios::binary)
        .write(reinterpret_cast<const char *>(collision.data()), static_cast<std::streamsize>(collision.size()));

    // the world: its room table (one room, id 0: its collision member 2, its model member 3), packed
    std::vector<unsigned char> table(8 + 0x28, 0);
    table[0] = 1;
    put32(table, 4, 8);
    table[8] = 0;     // id
    table[9] = 0;     // exits
    table[10] = 1;    // objects
    table[11] = 2;    // collision
    put32(table, 8 + 0x14, 0x1234);  // a word not read, kept
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
    CHECK(khdays_rooms_model("mi/wd/wd_eh#5/mdl/eh_00", path, sizeof(path)) && std::strstr(path, "room.gltf") != nullptr);
    CHECK(!khdays_rooms_model("mi/wd/wd_eh#3/mdl/eh_00", path, sizeof(path)));
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
