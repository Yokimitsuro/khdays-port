// The rooms of the KINGDOM HEARTS games in the player's collections: KH1, KH2 and Re:Chain of Memories from
// KINGDOM HEARTS HD 1.5+2.5 ReMIX, Dream Drop Distance from KINGDOM HEARTS HD 2.8 Final Chapter Prologue. A map is
// found by testing the names its game's pattern makes against a package's index (each entry keyed by the MD5 of its
// path), so no list of the games' files is kept here; it is taken out by the installer's own reader
// (installer/collection_archive.py), with the game's own executable for the packages' cipher, into the studio's
// collections folder (saves/collection unless set), <game>/original/<path> and <game>/remastered/<path>/<asset>. What
// is made holds Square Enix's work: it stays the player's own.
#pragma once

#include "core/mods/settings.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace studio {

enum class CollectionGame { kh1, kh2, recom, ddd, bbs };

// A game whose rooms are read: its code (a room of it is named "<code>:<map>"), its name, its collection's key in the
// install's record (setup/state.json: "1.5+2.5", "2.8"), its own executable, the packages its maps are in, and one of
// its units in Days's (KH1's and KH2's are centimetres, 0.01: KH1's stairs rise 20; Re:Chain of Memories' and Dream
// Drop Distance's metres, as Days's: their stairs rise 0.17 to 0.28; 0 while not measured), and the space all its maps
// took out of Steam's copy and made, and how many they were (collection_plan()'s estimate).
struct CollectionGameInfo {
    CollectionGame game;
    const char* code;
    const char* name;
    const char* collection;
    const char* executable;
    std::vector<const char*> packages;
    float units;
    std::uintmax_t all_bytes;
    std::size_t all_maps;
};
const std::vector<CollectionGameInfo>& collection_games();
const CollectionGameInfo* collection_game(const std::string& code);

// A world of `game` (its code: KH1's, KH2's, Birth by Sleep's and Dream Drop Distance's two letters, Re:Chain of
// Memories' MAP<nn>) by the name its game gives it, empty when the table has none; and as the studio lists it, that
// name with its code after it, else the code alone.
std::string collection_world_name(CollectionGame game, const std::string& world);
std::string collection_world_label(CollectionGame game, const std::string& world);

// A map of a game in the copy: its world, its name, the package holding it and its files' paths (its model first).
struct CollectionMap {
    std::string world, name, package;
    std::vector<std::string> paths;
};

// A package's index (its .hed: 32-byte entries, the first 16 bytes the MD5 of the entry's path) as a set of keys, and
// whether it holds `path`.
std::unordered_set<std::string> package_keys(const std::vector<std::uint8_t>& hed);
bool package_holds(const std::unordered_set<std::string>& keys, const std::string& path);

// The maps of `game` a package with index `keys` holds, by world then name (every world code of two letters and every
// number the game's names are written with), each with the paths of its files the package holds.
std::vector<CollectionMap> maps_in_package(const CollectionGameInfo& game, const std::string& package,
                                           const std::unordered_set<std::string>& keys);

// A collection's folder as a player gives it: the one holding its Image folder, or the Xbox app's game folder, whose
// Content holds it (installer/xbox_library.py), taken as that Content.
std::filesystem::path collection_root(const std::filesystem::path& folder);

// The folder of the collection holding `game`: the studio's setting (collection_1525, collection_28), else the one the
// install recorded (setup/state.json) with the game's executable there; empty when none is.
std::filesystem::path collection_copy(const Settings& settings, const CollectionGameInfo& game);

// Where the collections' maps are kept once taken out: the setting `collections` (saves/collection when empty),
// resolved against the port's folder.
std::filesystem::path collections_folder(const Settings& settings);

// Where a game's maps are kept once taken out: collections_folder()/<code>.
std::filesystem::path collection_folder(const Settings& settings, const CollectionGameInfo& game);

// The maps of `game` the player's copy holds, by world then name; false with the reason (no copy, no package).
bool collection_maps(const Settings& settings, const CollectionGameInfo& game, std::vector<CollectionMap>* maps,
                     std::string* why);

// Whether `map` is a stage the player falls through rather than walks (Dream Drop Distance's Dive, its worlds' _60 to
// _62 maps): none of its files is a collision.
bool collection_map_falling(const CollectionGameInfo& game, const CollectionMap& map);

// Whether `map`'s model file is in collection_folder() already.
bool collection_map_taken_out(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map);

// `map`'s files (and their remastered assets) taken out of the copy into collection_folder() unless they are there;
// with `extra`, more paths of the same package (a map's textures, once its model names them). False with the reason.
bool take_out_collection_map(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                             std::string* why, const std::vector<std::string>& extra = {});

// What making a room's model did: its triangles and pictures, its moving pieces (KH2's: placed, and those that move),
// whether its own game's collision was made, and what was left out.
struct CollectionMade {
    std::size_t triangles = 0;
    std::size_t pictures = 0;
    std::size_t pieces = 0;
    std::size_t animations = 0;
    bool collision = false;
    std::vector<std::string> report;
};

// Where a map's model is made: collection_folder()/made/<map>/<map>.gltf; its own collision beside it
// (<map>_collision.gltf).
std::filesystem::path collection_made_model(const Settings& settings, const CollectionGameInfo& game,
                                            const std::string& map);
std::filesystem::path collection_made_collision(const Settings& settings, const CollectionGameInfo& game,
                                                const std::string& map);

// `map` taken out of the copy (with the textures its model names) and made into collection_made_model(): a glTF and
// its pictures as PNG beside it, and its own game's floors and walls into collection_made_collision() where the map has
// them; false with the reason. KH2's maps as make_kh2_room and make_kh2_collision make them. What it did is kept
// beside the model (<map>.made.txt) for collection_room().
bool make_collection_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                          CollectionMade* made, std::string* why);

// Whether `map`'s model is made in collection_folder() by this build of the studio: it and its record written after the
// studio's executable was. A later studio may read the map better: its model is made again.
bool collection_room_ready(const Settings& settings, const CollectionGameInfo& game, const std::string& map);

// `map`'s model as make_collection_room makes it, or, when collection_room_ready(), the one there, what its making did
// read back from its record: nothing taken out or made again.
bool collection_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                     CollectionMade* made, std::string* why);

// What taking out every map of `game` would take: its maps in the copy (none, with the reason in `why`, when it cannot
// be read), how many are ready already (collection_room_ready()), the space collection_folder() takes now, and about
// about what the rest would add: the game's whole measured space (all_bytes, for as many maps as the copy holds) less
// what the folder holds already (a game's first maps are no measure of its others: twelve of Birth by Sleep's took 13
// MB each, all of them 23).
struct CollectionPlan {
    const CollectionGameInfo* game = nullptr;
    std::vector<CollectionMap> maps;
    std::size_t ready = 0;
    std::uintmax_t used = 0;
    std::uintmax_t more = 0;
    std::string why;
};
CollectionPlan collection_plan(const Settings& settings, const CollectionGameInfo& game);

// The space `bytes` take, as a person reads it: GB with one decimal from one GB, else MB.
std::string space_text(std::uintmax_t bytes);

// Told how far taking out every map is: what is being done (the package being read, the map being made) and how many
// of `total` steps are done; false from it stops the run after the step under way.
using CollectionStep = std::function<bool(const std::string& doing, std::size_t done, std::size_t total)>;

// Every map of `plan` not ready taken out of the copy, a package's maps a few at a time in each run of the reader, then
// made into its model (the textures its model names taken out then). The maps that could not be made are named with the
// reason in `failed`. False, with the reason, when the copy cannot be read or `step` stopped the run.
bool take_out_every_map(const Settings& settings, const CollectionPlan& plan, const CollectionStep& step,
                        std::vector<std::string>* failed, std::string* why);

// The installer's reader of the collections' packages run with `args` after its script: the install's own Python
// (setup/python), else the system's. Its exit status (-1: it did not start) and its output lines.
int run_collection_reader(const std::filesystem::path& root, const std::vector<std::string>& args,
                          std::vector<std::string>* lines, std::string* why);

// The folders the install's record names as its `key` input ("1.5+2.5", "2.8"), in its order.
std::vector<std::filesystem::path> recorded_collections(const std::filesystem::path& root, const std::string& key);

}  // namespace studio
