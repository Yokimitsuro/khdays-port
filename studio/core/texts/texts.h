// The game's texts as the studio edits them, and written back: string tables, message databases and the
// story scenes' lines (docs/FORMATS.md, "Text"), control codes kept as the game has them.
#pragma once

#include "core/files/containers.h"
#include "core/missions/missions.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

// A line of a story scene: a table of its script's data, a word per language (GetLanguage's 1 en .. 5 es)
// holding the offset of the line's UTF-8 text or -1, pointed at by a step's mode-0x40 operand.
struct SceneLine {
    std::size_t step = 0, slot = 0;  // the first step showing it, and its operand
    std::uint32_t table = 0;
};

struct TextFile {
    bool database = false;
    bool scenes = false;                             // the story's scenes (ev/EV_<world>.p2)
    int language = 1;                                // scenes: the language `lists` holds (1 en .. 5 es)
    std::vector<std::vector<std::u16string>> lists;  // a string table: one list; scenes: one per entry
    std::uint8_t lz = 0;                             // a string table's LZ type (0x10, 0x11), 0 when stored plain
    std::size_t header_size = 8;                     // a string table's header
    P2File p2;                                       // a database's or the scenes' container, each entry's LZ kept
    std::vector<Script> scripts;                     // scenes: each entry's script
    std::vector<std::vector<SceneLine>> lines;       // scenes: each script's lines, in step order
};

bool parse_text_file(const std::vector<std::uint8_t>& bytes, bool database, TextFile* out, std::string* why);
// A scene archive's lines in `language`: a list per entry, each line's text where its table points.
bool parse_scene_file(const std::vector<std::uint8_t>& bytes, int language, TextFile* out, std::string* why);
// A scene line whose text changed is written past its script's data and the table's word for the
// language points at it; the other languages keep theirs, and an entry not changed keeps its bytes.
std::vector<std::uint8_t> write_text_file(const TextFile& t);

// A text item's file is a database (db/db_<lang>.p2) rather than a string table.
bool is_text_database(const std::string& game_path);
// A text item's file is a scene archive (ev/EV_<world>.p2).
bool is_scene_archive(const std::string& game_path);

// The characters of `text` the font cannot draw (control codes left out),
// each once, in order.
std::u16string missing_glyphs(const std::u16string& text, const std::function<bool(char16_t)>& font_has);

// --check-texts: every text file of the game read and written back the same
// bytes (decompressed), every string's count said.
bool check_all_texts(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

}  // namespace studio
