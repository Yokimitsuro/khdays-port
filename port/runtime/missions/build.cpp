// A mod's mission built at the start from its description (missions.txt's waves, start, time, texts), out of the
// game's own mission files as the ROM holds them: khdays-studio's builder (studio/core/missions/mission_build.h),
// compiled in as it is.
#include "build.h"

#include "../core/rom.h"

#include "core/missions/mission_build.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// The game's files through the ROM, its mission files' numbers ("mi/mi/<digits>") and its overlay table (the
// header's 0x50 and 0x54: where and how long).
studio::GameFiles rom_game_files()
{
    studio::GameFiles g;
    g.read = [](const std::string &path) {
        static KhdaysRomReader *reader = khdays_rom_reader_new();
        u8 *data = nullptr;
        u32 size = 0;
        std::vector<std::uint8_t> out;
        if (khdays_rom_file_data(reader, path.c_str(), -1, &data, &size)) {
            out.assign(data, data + size);
            std::free(data);
        }
        return out;
    };
    for (int id = 0; id < khdays_rom_file_count(); ++id) {
        char path[256];
        u32 offset, bytes;
        if (!khdays_rom_file_info(id, path, static_cast<int>(sizeof(path)), &offset, &bytes) ||
            std::strncmp(path, "mi/mi/", 6) != 0 || path[6] == '\0') {
            continue;
        }
        const char *digits = path + 6;
        if (std::strspn(digits, "0123456789") == std::strlen(digits)) {
            g.missions.push_back(std::atoi(digits));
        }
    }
    const u8 *header = khdays_rom_header();
    auto word = [header](int at) {
        return static_cast<u32>(header[at] | header[at + 1] << 8 | header[at + 2] << 16 |
                                static_cast<u32>(header[at + 3]) << 24);
    };
    g.overlay_table.resize(word(0x54));
    if (!g.overlay_table.empty()) {
        khdays_rom_read(word(0x50), g.overlay_table.data(), static_cast<u32>(g.overlay_table.size()));
    }
    return g;
}

} // namespace

extern "C" int khdays_missions_build(const KhdaysMission *m, unsigned char **out, size_t *size, char *why,
                                     size_t why_size)
{
    studio::MissionPlan plan;
    std::vector<std::uint8_t> bytes;
    std::string reason;
    static const studio::GameFiles game = rom_game_files();
    if (!studio::plan_of(*m, &plan, &reason) || !studio::build_mission(game, plan, &bytes, &reason)) {
        std::snprintf(why, why_size, "%s", reason.c_str());
        return 0;
    }
    *out = static_cast<unsigned char *>(std::malloc(bytes.size()));
    if (*out == nullptr) {
        std::snprintf(why, why_size, "not enough memory");
        return 0;
    }
    std::memcpy(*out, bytes.data(), bytes.size());
    *size = bytes.size();
    return 1;
}
