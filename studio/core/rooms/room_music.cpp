#include "core/rooms/room_music.h"

#include "core/files/paths.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace studio {

namespace {

constexpr const char* kExtensions[] = {".wav", ".flac", ".ogg"};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

RoomMusicPlace game_room_music(const fs::path& mod_dir, const std::string& world, int room) {
    return {mod_dir / "sounds" / "rooms" / path_u8(world), std::to_string(room), std::to_string(room) + "_battle"};
}

RoomMusicPlace added_room_music(const fs::path& room_folder) {
    return {room_folder, "music", "music_battle"};
}

fs::path room_music_file(const RoomMusicPlace& place, bool battle) {
    std::error_code ec;
    for (const char* extension : kExtensions) {
        const fs::path file = place.folder / path_u8((battle ? place.battle : place.field) + extension);
        if (fs::is_regular_file(file, ec))
            return file;
    }
    return {};
}

bool room_music_kind(const fs::path& file) {
    const std::string extension = lower(u8(file.extension()));
    return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions);
}

bool room_music_set(const RoomMusicPlace& place, bool battle, const fs::path& source, std::string* why) {
    std::error_code ec;
    if (!room_music_kind(source)) {
        *why = u8(source.filename()) + ": not a WAV, FLAC or Ogg file";
        return false;
    }
    if (!fs::is_regular_file(source, ec)) {
        *why = u8(source) + ": no such file";
        return false;
    }
    const fs::path to = place.folder / path_u8((battle ? place.battle : place.field) + lower(u8(source.extension())));
    fs::create_directories(place.folder, ec);
    if (ec) {
        *why = u8(place.folder) + ": " + ec.message();
        return false;
    }
    if (!fs::equivalent(source, to, ec))
        fs::copy_file(source, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        *why = u8(to) + ": " + ec.message();
        return false;
    }
    // one file a room's field (or battle): the others of other extensions go
    for (const char* extension : kExtensions) {
        const fs::path other = place.folder / path_u8((battle ? place.battle : place.field) + extension);
        if (other != to)
            fs::remove(other, ec);
    }
    return true;
}

bool room_music_remove(const RoomMusicPlace& place, bool battle, std::string* why) {
    for (const char* extension : kExtensions) {
        std::error_code ec;
        const fs::path file = place.folder / path_u8((battle ? place.battle : place.field) + extension);
        if (fs::exists(file, ec) && !fs::remove(file, ec)) {
            *why = u8(file) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

}  // namespace studio
