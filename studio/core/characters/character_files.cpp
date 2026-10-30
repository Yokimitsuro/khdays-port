// A mod's characters (character_files.h).
#include "core/characters/character_files.h"

#include "core/files/file_write.h"
#include "core/files/paths.h"
#include "core/models/model_files.h"

#include "khdays/assets/image.h"

extern "C" {
#include "characters/registry.h"
#include "render/png_io.h"
}

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

namespace fs = std::filesystem;

namespace studio {

namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// A key with its spaces closed up: "name  es" -> "name es".
std::string key_of(const std::string& raw) {
    std::string out;
    bool space = false;
    for (const char c : trim(raw)) {
        if (c == ' ' || c == '\t') {
            space = true;
            continue;
        }
        if (space && !out.empty())
            out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// A line's key, value and comment ("  # ..."); false for a comment, a part or a blank line.
bool split_line(const std::string& line, std::string* key, std::string* value, std::string* comment) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == '[')
        return false;
    const auto eq = t.find('=');
    if (eq == std::string::npos)
        return false;
    *key = key_of(t.substr(0, eq));
    std::string rest = t.substr(eq + 1);
    comment->clear();
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == '#' && (i == 0 || rest[i - 1] == ' ' || rest[i - 1] == '\t')) {
            *comment = rest.substr(i);
            rest = rest.substr(0, i);
            break;
        }
    }
    *value = trim(rest);
    return true;
}

// A part's name from "[name]" (its comment after it left out); false for any other line.
bool part_line(const std::string& line, std::string* name) {
    const std::string t = trim(line);
    if (t.empty() || t[0] != '[')
        return false;
    const auto close = t.find(']');
    if (close == std::string::npos)
        return false;
    *name = trim(t.substr(1, close - 1));
    return true;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

struct Problems {
    std::vector<std::string>* lines;
};

void reported(const char*, const char*, int line, const char* why, void* context) {
    auto* p = static_cast<Problems*>(context);
    p->lines->push_back(line > 0 ? "line " + std::to_string(line) + ": " + why : std::string(why));
}

bool lower_extension_is(const fs::path& p, std::initializer_list<const char*> wanted) {
    std::string e = u8(p.extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* w : wanted)
        if (e == w)
            return true;
    return false;
}

}  // namespace

IniText IniText::parse(const std::string& text) {
    IniText out;
    out.parts_.push_back({});
    std::size_t at = 0;
    while (at <= text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string name;
        if (part_line(line, &name))
            out.parts_.push_back({name, {}});
        out.parts_.back().lines.push_back(line);
        if (end == text.size())
            break;
        at = end + 1;
    }
    // a text ending in a line break: no empty line after it
    if (!out.parts_.back().lines.empty() && out.parts_.back().lines.back().empty() && !text.empty() &&
        text.back() == '\n')
        out.parts_.back().lines.pop_back();
    return out;
}

std::string IniText::text() const {
    std::string out;
    for (const Part& p : parts_)
        for (const std::string& l : p.lines)
            out += l + "\n";
    return out;
}

const IniText::Part* IniText::find(const std::string& name) const {
    for (const Part& p : parts_)
        if (p.name == name)
            return &p;
    return nullptr;
}

IniText::Part& IniText::part(const std::string& name) {
    for (Part& p : parts_)
        if (p.name == name)
            return p;
    // a new part at the end, a blank line before it
    Part& last = parts_.back();
    if (!last.lines.empty() && !trim(last.lines.back()).empty())
        last.lines.emplace_back();
    parts_.push_back({name, {"[" + name + "]"}});
    return parts_.back();
}

bool IniText::has_part(const std::string& name) const {
    return find(name) != nullptr;
}

std::string IniText::get(const std::string& part_name, const std::string& key, bool* found) const {
    if (found != nullptr)
        *found = false;
    const Part* p = find(part_name);
    if (p == nullptr)
        return {};
    const std::string wanted = key_of(key);
    for (const std::string& l : p->lines) {
        std::string k, v, c;
        if (split_line(l, &k, &v, &c) && k == wanted) {
            if (found != nullptr)
                *found = true;
            return v;
        }
    }
    return {};
}

void IniText::set(const std::string& part_name, const std::string& key, const std::string& value) {
    Part& p = part(part_name);
    const std::string wanted = key_of(key);
    for (std::string& l : p.lines) {
        std::string k, v, c;
        if (split_line(l, &k, &v, &c) && k == wanted) {
            l = wanted + " = " + value + (c.empty() ? "" : "   " + c);
            return;
        }
    }
    // after the part's last line that is not blank
    std::size_t at = p.lines.size();
    while (at > 1 && trim(p.lines[at - 1]).empty())
        --at;
    p.lines.insert(p.lines.begin() + static_cast<std::ptrdiff_t>(at), wanted + " = " + value);
}

void IniText::set_noted(const std::string& part_name, const std::string& key, const std::string& value,
                        const std::string& note) {
    set(part_name, key, value);
    Part& p = part(part_name);
    const std::string wanted = key_of(key);
    for (std::string& l : p.lines) {
        std::string k, v, c;
        if (split_line(l, &k, &v, &c) && k == wanted) {
            l = wanted + " = " + value + (note.empty() ? "" : "   # " + note);
            return;
        }
    }
}

void IniText::remove(const std::string& part_name, const std::string& key) {
    for (Part& p : parts_) {
        if (p.name != part_name)
            continue;
        const std::string wanted = key_of(key);
        p.lines.erase(std::remove_if(p.lines.begin(), p.lines.end(),
                                     [&](const std::string& l) {
                                         std::string k, v, c;
                                         return split_line(l, &k, &v, &c) && k == wanted;
                                     }),
                      p.lines.end());
    }
}

std::vector<std::pair<std::string, std::string>> IniText::entries(const std::string& part_name) const {
    std::vector<std::pair<std::string, std::string>> out;
    const Part* p = find(part_name);
    if (p == nullptr)
        return out;
    for (const std::string& l : p->lines) {
        std::string k, v, c;
        if (split_line(l, &k, &v, &c))
            out.emplace_back(k, v);
    }
    return out;
}

void IniText::set_entries(const std::string& part_name,
                          const std::vector<std::pair<std::string, std::string>>& entries) {
    if (find(part_name) == nullptr && entries.empty())
        return;
    Part& p = part(part_name);
    std::vector<std::string> kept;
    for (const std::string& l : p.lines) {
        std::string k, v, c;
        if (!split_line(l, &k, &v, &c))
            kept.push_back(l);
    }
    while (kept.size() > 1 && trim(kept.back()).empty())
        kept.pop_back();
    const bool blank_after = &p != &parts_.back();
    if (entries.empty() && kept.size() == 1) {
        // nothing left but its heading: the part goes, with the blank line before it
        for (std::size_t i = 0; i < parts_.size(); ++i) {
            if (&parts_[i] != &p)
                continue;
            parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
            Part& before = parts_[i - 1];
            while (!before.lines.empty() && trim(before.lines.back()).empty())
                before.lines.pop_back();
            if (i < parts_.size())
                before.lines.emplace_back();
            return;
        }
    }
    for (const auto& [k, v] : entries)
        kept.push_back(key_of(k) + " = " + v);
    if (blank_after)
        kept.emplace_back();
    p.lines = std::move(kept);
}

std::vector<std::string> IniText::part_names() const {
    std::vector<std::string> out;
    for (std::size_t i = 1; i < parts_.size(); ++i)
        out.push_back(parts_[i].name);
    return out;
}

// One blank line between parts, none at the text's end.
void IniText::space_parts() {
    for (std::size_t i = 0; i < parts_.size(); ++i) {
        auto& lines = parts_[i].lines;
        while (lines.size() > (i == 0 ? 0u : 1u) && trim(lines.back()).empty())
            lines.pop_back();
        if (i + 1 < parts_.size() && !lines.empty())
            lines.emplace_back();
    }
}

void IniText::rename_part(const std::string& from, const std::string& to) {
    for (std::size_t i = 1; i < parts_.size(); ++i) {
        if (parts_[i].name != from)
            continue;
        parts_[i].name = to;
        std::string& head = parts_[i].lines.front();
        const auto close = head.find(']');
        head = "[" + to + (close == std::string::npos ? "]" : head.substr(close));  // its comment kept
        return;
    }
}

void IniText::remove_part(const std::string& name) {
    for (std::size_t i = 1; i < parts_.size(); ++i) {
        if (parts_[i].name == name) {
            parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
            space_parts();
            return;
        }
    }
}

void IniText::copy_part(const std::string& from, const std::string& to) {
    const Part* p = find(from);
    if (p == nullptr || p == &parts_.front())
        return;
    Part copied = *p;
    const auto close = copied.lines.front().find(']');
    copied.lines.front() = "[" + to + (close == std::string::npos ? "]" : copied.lines.front().substr(close));
    copied.name = to;
    parts_.push_back(std::move(copied));
    space_parts();
}

void IniText::move_parts(const std::vector<std::string>& names, const std::string& before) {
    std::vector<Part> moved;
    for (const auto& n : names) {
        for (std::size_t i = 1; i < parts_.size(); ++i) {
            if (parts_[i].name == n) {
                moved.push_back(std::move(parts_[i]));
                parts_.erase(parts_.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    std::size_t at = parts_.size();
    for (std::size_t i = 1; i < parts_.size(); ++i) {
        if (parts_[i].name == before) {
            at = i;
            break;
        }
    }
    parts_.insert(parts_.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(moved.begin()),
                  std::make_move_iterator(moved.end()));
    space_parts();
}

fs::path character_dir(const fs::path& mod_dir, const std::string& key) {
    return mod_dir / "characters" / path_u8(key);
}

std::vector<std::string> character_keys(const fs::path& mod_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(mod_dir / "characters", ec)) {
        if (e.is_directory(ec) && fs::is_regular_file(e.path() / "character.ini", ec))
            out.push_back(u8(e.path().filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool valid_character_key(const std::string& key) {
    if (key.empty())
        return false;
    for (const char c : key)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
            return false;
    return true;
}

const std::vector<std::string>& character_bases() {
    static const std::vector<std::string> bases = [] {
        std::vector<std::string> b;
        for (int i = 0; i < KHDAYS_CHARACTER_BASES; ++i)
            b.emplace_back(khdays_character_base_name(i));
        return b;
    }();
    return bases;
}

std::string character_base_code(const std::string& base) {
    for (int i = 0; i < KHDAYS_CHARACTER_BASES; ++i)
        if (base == khdays_character_base_name(i))
            return khdays_character_base_code(i);
    return {};
}

std::string mod_id_of(const fs::path& mod_dir) {
    const IniText ini = IniText::parse(read_text(mod_dir / "mod.ini"));
    const std::string id = ini.get("", "id");
    return id.empty() ? u8(mod_dir.filename()) : id;
}

std::vector<std::string> check_character(const fs::path& mod_dir, const std::string& key, const std::string& text) {
    std::vector<std::string> lines;
    Problems problems{&lines};
    KhdaysCharacters r{};
    const std::string id = mod_id_of(mod_dir);
    const std::string folder = u8(mod_dir.filename());
    const bool taken = khdays_characters_read(&r, id.c_str(), folder.c_str(), key.c_str(), text.data(), text.size(),
                                              reported, &problems) != 0;
    if (taken && r.count > 0) {
        const KhdaysCharacter& c = r.list[r.count - 1];
        const fs::path dir = character_dir(mod_dir, key);
        std::error_code ec;
        auto look = [&](const char* what, const char* file) {
            if (file != nullptr && !fs::is_regular_file(dir / path_u8(file), ec))
                lines.push_back(std::string(what) + ": " + file + " is not in the character's folder");
        };
        look("face", c.face);
        // the character's files and each form's (what a form takes from its default looked for once)
        std::set<std::string> seen;
        for (int f = -1; f < c.form_count; ++f) {
            const KhdaysCharacter& t = f < 0 ? c : c.forms[f];
            const std::string where = f < 0 ? std::string() : "[form " + std::string(t.form) + "] ";
            auto look_once = [&](const std::string& what, const char* file) {
                if (file != nullptr && seen.insert(file).second)
                    look((where + what).c_str(), file);
            };
            look_once("model", t.model);
            look_once("fallback", t.fallback);
            for (int i = 0; i < t.action_count; ++i)
                look_once(t.actions[i].game, t.actions[i].clip);
            for (int i = 0; i < t.sound_count; ++i)
                look_once(t.sounds[i].game, t.sounds[i].file);
            for (int i = 0; i < t.weapon_count; ++i)
                look_once(t.weapons[i].game, t.weapons[i].file);
        }
    } else if (lines.empty()) {
        lines.push_back("not taken");
    }
    khdays_characters_free(&r);
    return lines;
}

bool new_character(const fs::path& mod_dir, const std::string& key, const std::string& base, const std::string& name,
                   std::string* why) {
    if (!valid_character_key(key))
        return fail_with(why, "a key is letters, digits, '_' and '-'");
    if (character_base_code(base).empty())
        return fail_with(why, "no base " + base);
    if (name.empty() || name.find('#') != std::string::npos)
        return fail_with(why, "a name, without '#'");
    std::error_code ec;
    if (fs::exists(character_dir(mod_dir, key), ec))
        return fail_with(why, "the mod has a character " + key + " already");
    const std::string text = "# " + name +
                             " (khdays-studio; docs/CHARACTERS.md)\n[character]\nformat = 1\nbase = " + base +
                             "\nname = " + name + "\n\n[model]\n";
    // its model is still to be given: written as it is, checked once it has one
    const fs::path file = character_dir(mod_dir, key) / "character.ini";
    const WriteResult w = write_into_mod(
        mod_dir, file,
        [&](const fs::path& temporary) {
            std::ofstream out(temporary, std::ios::binary);
            out << text;
            return out ? std::string() : "cannot be written";
        },
        [&](const fs::path& temporary) { return read_text(temporary) == text ? std::string() : "not as written"; });
    return w.ok || fail_with(why, w.why);
}

bool copy_into_character(const fs::path& mod_dir, const std::string& key, const fs::path& source,
                         const std::string& subfolder, std::string* relative, std::string* why) {
    std::error_code ec;
    if (!fs::is_regular_file(source, ec))
        return fail_with(why, u8(source) + " is not a file");
    const fs::path dir = character_dir(mod_dir, key) / path_u8(subfolder);
    const bool picture = subfolder.empty() && !model_extension(u8(source.extension()));
    fs::path name = source.filename();
    // a picture not in PNG made one (the port reads a face as PNG)
    if (picture && !lower_extension_is(source, {".png"}))
        name.replace_extension(".png");
    const fs::path to = dir / name;
    *relative = (subfolder.empty() ? "" : subfolder + "/") + u8(name);
    if (fs::exists(to, ec) && fs::equivalent(source, to, ec))
        return true;  // already there
    WriteResult w;
    if (picture && !lower_extension_is(source, {".png"})) {
        const auto image = khdays::assets::read_image(source);
        if (image.rgba.empty() || image.width <= 0 || image.height <= 0)
            return fail_with(why, u8(source) + ": not a picture the tool reads");
        w = write_into_mod(
            mod_dir, to,
            [&](const fs::path& temporary) {
                const char* e = khdays_png_write(u8(temporary).c_str(), image.rgba.data(), image.width, image.height);
                return e == nullptr ? std::string() : std::string(e);
            },
            [&](const fs::path& temporary) {
                int width = 0, height = 0;
                const char* e = nullptr;
                std::uint8_t* px = khdays_png_read(u8(temporary).c_str(), &width, &height, &e);
                khdays_png_free(px);
                return px != nullptr && width == image.width && height == image.height ? std::string()
                                                                                       : "not as written";
            });
        return w.ok || fail_with(why, w.why);
    }
    std::vector<std::pair<fs::path, fs::path>> files{{source, to}};
    if (model_extension(u8(source.extension()))) {
        std::vector<std::string> not_found;
        for (const Companion& c : model_companions(source, &not_found))
            files.emplace_back(c.source, dir / path_u8(c.relative));
    }
    for (const auto& [from, into] : files) {
        if (fs::exists(into, ec) && fs::equivalent(from, into, ec))
            continue;
        w = write_into_mod(
            mod_dir, into, [&](const fs::path& temporary) { return copy_bytes(from, temporary); },
            [&](const fs::path& temporary) {
                return read_text(temporary) == read_text(from) ? std::string() : "the copy differs";
            });
        if (!w.ok)
            return fail_with(why, u8(from.filename()) + ": " + w.why);
    }
    return true;
}

bool save_character(const fs::path& mod_dir, const std::string& key, const std::string& text, std::string* why) {
    const std::vector<std::string> problems = check_character(mod_dir, key, text);
    if (!problems.empty())
        return fail_with(why, problems.front());
    const fs::path file = character_dir(mod_dir, key) / "character.ini";
    const WriteResult w = write_into_mod(
        mod_dir, file,
        [&](const fs::path& temporary) {
            std::ofstream out(temporary, std::ios::binary);
            out << text;
            return out ? std::string() : "cannot be written";
        },
        [&](const fs::path& temporary) {
            const std::string back = read_text(temporary);
            if (back != text)
                return std::string("not as written");
            const auto again = check_character(mod_dir, key, back);
            return again.empty() ? std::string() : again.front();
        });
    return w.ok || fail_with(why, w.why);
}

std::vector<std::string> character_files(const fs::path& mod_dir, const std::string& key,
                                         const std::string& subfolder) {
    std::vector<std::string> out;
    const fs::path dir = character_dir(mod_dir, key) / path_u8(subfolder);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && lower_extension_is(e.path(), {".fbx", ".gltf", ".glb"}))
            out.push_back(subfolder + "/" + u8(e.path().filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace studio
