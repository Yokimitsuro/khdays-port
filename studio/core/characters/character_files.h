// A mod's characters (mods/<Mod>/characters/<key>/, docs/CHARACTERS.md): their character.ini read and changed
// line by line (what the tool does not change -- comments, parts it does not know -- kept as written), made new,
// given a model, clips and a face copied in, and checked by the port's own reader
// (port/runtime/characters/registry.c) before they are called saved.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// An ini text as written: its parts ([name]) in order, each a list of lines; changing a value rewrites its line
// only. A key is what is before its '=' with its spaces closed up ("name  es" is "name es").
class IniText {
public:
    static IniText parse(const std::string& text);
    std::string text() const;

    // A part's key's value (its comment after " #" left out); empty when missing (*found false).
    std::string get(const std::string& part, const std::string& key, bool* found = nullptr) const;
    // Set: its line rewritten (its comment kept), else added at the part's end (the part made at the text's end).
    void set(const std::string& part, const std::string& key, const std::string& value);
    void remove(const std::string& part, const std::string& key);
    // Every key = value of a part, in order.
    std::vector<std::pair<std::string, std::string>> entries(const std::string& part) const;
    // A part's key lines replaced by these, in order (its comment lines kept before them); none: the part left
    // with its comments, or taken away when it has none.
    void set_entries(const std::string& part, const std::vector<std::pair<std::string, std::string>>& entries);
    bool has_part(const std::string& part) const;

private:
    struct Part {
        std::string name;  // empty: the lines before the first part
        std::vector<std::string> lines;
    };
    std::vector<Part> parts_;
    const Part* find(const std::string& name) const;
    Part& part(const std::string& name);
};

// The characters/<key>/ folders of a mod that hold a character.ini, sorted.
std::vector<std::string> character_keys(const std::filesystem::path& mod_dir);
std::filesystem::path character_dir(const std::filesystem::path& mod_dir, const std::string& key);

// A key is letters, digits, '_' and '-'.
bool valid_character_key(const std::string& key);

// The game's characters a new one plays as: their names in a description (roxas ...), 19 of them, and each one's
// folder code (ro ...).
const std::vector<std::string>& character_bases();
std::string character_base_code(const std::string& base);

// A new character `key` in the mod: its folder and its character.ini (format 1, `base`, `name`), read back. False
// with the reason (a key taken among them).
bool new_character(const std::filesystem::path& mod_dir, const std::string& key, const std::string& base,
                   const std::string& name, std::string* why);

// `source` (and the files it needs beside it: a model's buffers and textures) copied into the character's
// `subfolder` (model, clips, ...); *relative its path from the character's folder ("model/x.fbx"). A file already
// there under that name is replaced. False with the reason.
bool copy_into_character(const std::filesystem::path& mod_dir, const std::string& key,
                         const std::filesystem::path& source, const std::string& subfolder, std::string* relative,
                         std::string* why);

// The character's description `text` read as the port reads it, and its files looked for: each problem a line (the
// port's words, with the line it is on); none when the port takes it as it is.
std::vector<std::string> check_character(const std::filesystem::path& mod_dir, const std::string& key,
                                         const std::string& text);

// `text` written as the character's character.ini when the port takes it (check_character), read back. False with
// the reason (the first problem).
bool save_character(const std::filesystem::path& mod_dir, const std::string& key, const std::string& text,
                    std::string* why);

// The character's files of a kind in its folder ("clips": .fbx .gltf .glb; relative paths, sorted).
std::vector<std::string> character_files(const std::filesystem::path& mod_dir, const std::string& key,
                                         const std::string& subfolder);

// The mod's id (mod.ini's `id`), else its folder's name.
std::string mod_id_of(const std::filesystem::path& mod_dir);

}  // namespace studio
