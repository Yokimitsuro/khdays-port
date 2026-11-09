// The game's replaceable content:
// graphics and sounds as the port's dumps name them, texts and
// movies from the extracted game files (R5).
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace studio {

enum class Kind { texture, sheet, model, sound, text_file, movie, room, mission };
constexpr int kKinds = 8;

struct Item {
    Kind kind = Kind::texture;
    // The replacement's path under its kind's mod folder, without extension:
    // `ba/ch/ro/def.p.z/tex/ro_head@ro_head_pl`, `seqarc/COMMON_SYS/3`; for
    // a text file its game path (`UI/btl/es/magic.s.z`), for a movie its id;
    // for a room of a mission world `<world file>/room/<its index in the table>`
    // (`mi/wd/wd_tw/room/0`: ids repeat within a world).
    std::string key;
    // The game file it comes from (`ba/ch/ro/def.p.z`, `UI/mlt/res.p2#0`);
    // for a sound its folder (`seqarc/COMMON_SYS`).
    std::string group;
    std::filesystem::path original;  // the dumped PNG/glTF/WAV, or the game file
    std::string language;            // texts: de, en, es, fr, it, ja; else empty
    std::string folded;              // the key in lower case, for search
};

struct CatalogSources {
    std::filesystem::path graphics_dump;
    std::filesystem::path sounds_dump;
    std::filesystem::path nitrofs;  // the extracted game files (data/extracted/<hash>/nitrofs)
};

struct Catalog {
    std::vector<Item> items;  // sorted by kind, then key
    bool graphics_dump_found = false;
    bool graphics_dump_models = false;  // it has models (the game's own Dump graphics setting writes none)
    bool sounds_dump_found = false;
    bool game_files_found = false;

    // Indices of the items whose key holds every word of `query` (any case),
    // of the kinds in `kinds` (bit 1 << int(Kind)); all of those kinds when
    // `query` is empty.
    std::vector<int> search(std::string_view query, unsigned kinds) const;
};

// Walks the sources. `progress` counts the files seen; `cancel` stops the
// walk early (what was found so far is returned).
Catalog build_catalog(const CatalogSources& sources, std::atomic<int>* progress, const std::atomic<bool>* cancel);

// The texts' language from their path: `_xx.` in the name or a `/xx/` folder.
std::string text_language(std::string_view game_path);

// A texture's key without its palette (`.../tex/<texture>@<palette>` ->
// `.../tex/<texture>`): the name the port also looks for, for every palette
// (port/runtime/mods/textures.c lines 216-226). Empty when the key has none.
std::string any_palette_key(const Item& item);

// The name an export of `item` is offered under: `main.p2#0_es.png`,
// `ro_b.gltf`, `wd_tw_room0.gltf`.
std::string export_name(const Item& item);

}  // namespace studio
