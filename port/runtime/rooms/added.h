/* The rooms the mods add to the game's mission worlds: mods/<Mod>/rooms/rooms.txt names each (a line
 * "<key> <world>"), its files in rooms/<world>/<key>/: room.gltf (or .glb, .fbx) the model drawn, collision.bin its
 * floors and walls in the game's form (the studio's --add-room writes both). When the game reads a world's archive
 * (mi/wd/wd_<code>), its added rooms are put after the world's own: each a room record of its own id (no exits: only
 * missions start there), its collision a member of its own and a copy of room 0's model a member of its own, drawn as
 * the mod's model (docs/RUNTIME_MAP.md, "A world's rooms"). A room with a map (map.png and map.txt, which the studio
 * draws from its floors) has it on the lower screen as the game's rooms do: its picture after the game's in
 * UI/btl/mapchr.p2, an entry of its id in its world's chain in each UI/btl/<language>/map.p2. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
#include <functional>
#include <string>
#include <vector>

/* What a merge reads of the game besides the file it serves: another of its files as the game has it (a world's
 * archive, the map pictures). */
struct KhdaysGameFiles {
    std::function<std::vector<unsigned char>(const std::string &game_path)> read;
};

/* Whether `game_path` is a file the mods' rooms may add to: a world's archive (mi/wd/wd_<code>), the map pictures
 * (UI/btl/mapchr.p2), a language's map chains (UI/btl/<language>/map.p2), a language's world names
 * (UI/cm/str/world_id_<language>.s.z). */
bool khdays_rooms_serves(const std::string &game_path);

/* `game_path`'s bytes `source` with what the mods' rooms add to it into `out`; false when nothing is added (a file
 * they do not add to, or none adds to it). */
bool khdays_rooms_merge(const std::string &game_path, const std::vector<unsigned char> &source,
                        std::vector<unsigned char> &out, const KhdaysGameFiles &game = {});

extern "C" {
#endif

/* The mods' added rooms and worlds read (once, after the mods are taken at the start). */
void khdays_rooms_load(void);

/* Mission Mode's world slots: the game's twelve, and past them (from 13) the worlds the mods add (rooms/worlds.txt:
 * "[world <key>]", "like = <a game world's code>" it is played in, "name <language> = ..."), each listed under its
 * own name (served in UI/cm/str/world_id_<language>.s.z). */
int khdays_mission_slots(void);

/* The world `key` the mod in `mod_path` adds: its Mission Mode slot, and the code of the game world it is played in
 * into `like` (3 bytes); -1 when none. */
int khdays_mod_world(const char *mod_path, const char *key, char like[3]);

/* Where on Mission Mode's world map slot `slot` is shown: a mod world's game world's slot, else itself. */
int khdays_mission_slot_shown(int slot);

/* The room `key` the mod in `mod_path` adds to `world` (its code): its index in the world's room table, the number
 * a mission names its room by; -1 when none. Known once its world's archive was read. */
int khdays_rooms_index(const char *mod_path, const char *key, const char *world);

/* Whether `name` (a model's, "mi/wd/wd_<code>#<member>/mdl/<model>") is an added room's: its mod's model into
 * `path`. */
int khdays_rooms_model(const char *name, char *path, int size);

#ifdef __cplusplus
}
#endif
