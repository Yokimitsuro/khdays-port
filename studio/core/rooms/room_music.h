// The music a mod gives a room, which the port plays in place of the game's while the player is in it
// (docs/SOUND.md, "A room's music"): a game room's in the mod's sounds/rooms/<world>/<room>.<ext> (its field's) and
// <room>_battle.<ext> (its battle's and its bosses'; without one the field's goes on), a room the mod adds in its
// own folder, music.<ext> and music_battle.<ext>. WAV, FLAC or Ogg Vorbis; a file with no loop of its own plays
// whole, round and round.
#pragma once

#include <filesystem>
#include <string>

namespace studio {

struct RoomMusicPlace {
    std::filesystem::path folder;
    std::string field, battle;  // the files' names, without their extension
};

// Room `room` (its index in the world's room table) of the game world `world` (its code: a mod's world's rooms are
// its game world's).
RoomMusicPlace game_room_music(const std::filesystem::path& mod_dir, const std::string& world, int room);
// A room the mod adds, by its folder (added_room_folder).
RoomMusicPlace added_room_music(const std::filesystem::path& room_folder);

// The file there for the field (or the battle), empty when there is none.
std::filesystem::path room_music_file(const RoomMusicPlace& place, bool battle);
// Whether a file can be a room's music: .wav, .flac or .ogg.
bool room_music_kind(const std::filesystem::path& file);
// `source` copied there for the field (or the battle), any other there of another extension taken away; false with
// the reason.
bool room_music_set(const RoomMusicPlace& place, bool battle, const std::filesystem::path& source, std::string* why);
// The file there taken away; false with the reason (none there is no failure).
bool room_music_remove(const RoomMusicPlace& place, bool battle, std::string* why);

}  // namespace studio
