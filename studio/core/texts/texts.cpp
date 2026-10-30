#include "core/texts/texts.h"

#include "core/files/lz.h"
#include "core/files/paths.h"
#include "core/missions/mission_edit.h"

#include "khdays/assets/message.h"  // lz_decompress

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

namespace studio {

namespace {

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint32_t>(d[at]) | static_cast<std::uint32_t>(d[at + 1]) << 8 |
           static_cast<std::uint32_t>(d[at + 2]) << 16 | static_cast<std::uint32_t>(d[at + 3]) << 24;
}

void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

void add16(std::vector<std::uint8_t>& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xff));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

// A database entry's list: count, each string's end (bytes), the strings.
bool parse_list(const std::vector<std::uint8_t>& blob, std::vector<std::u16string>* out, std::string* why) {
    if (blob.size() < 4)
        return fail_with(why, "a list shorter than its count");
    const std::uint32_t count = le32(blob, 0);
    const std::size_t base = (static_cast<std::size_t>(count) + 1) * 4;
    if (base > blob.size())
        return fail_with(why, "a list's ends past it");
    std::vector<std::u16string> strings;
    std::uint32_t start = 0;
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t end = le32(blob, 4 + 4 * static_cast<std::size_t>(k));
        if (end < start || base + end > blob.size() || (end & 1) != 0)
            return fail_with(why, "a string's end out of place");
        std::u16string text;
        for (std::uint32_t b = start; b < end; b += 2)
            text.push_back(static_cast<char16_t>(blob[base + b] | blob[base + b + 1] << 8));
        if (text.empty() || text.back() != u'\0')
            return fail_with(why, "a string without its end mark");
        text.pop_back();
        strings.push_back(std::move(text));
        start = end;
    }
    if (base + start != blob.size())
        return fail_with(why, "bytes after a list's last string");
    *out = std::move(strings);
    return true;
}

std::vector<std::uint8_t> write_list(const std::vector<std::u16string>& strings) {
    std::vector<std::uint8_t> d;
    add32(d, static_cast<std::uint32_t>(strings.size()));
    std::uint32_t end = 0;
    for (const auto& s : strings) {
        end += static_cast<std::uint32_t>((s.size() + 1) * 2);
        add32(d, end);
    }
    for (const auto& s : strings) {
        for (const char16_t c : s)
            add16(d, c);
        add16(d, 0);
    }
    return d;
}

// The scenes' texts are UTF-8; the view edits UTF-16, as the game's other texts.
std::u16string utf16_of(const std::string& s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        const int extra = b < 0x80 ? 0 : (b & 0xe0) == 0xc0 ? 1 : (b & 0xf0) == 0xe0 ? 2 : (b & 0xf8) == 0xf0 ? 3 : -1;
        char32_t c = extra == 0 ? b : extra == 1 ? b & 0x1f : extra == 2 ? b & 0x0f : b & 0x07;
        bool whole = extra >= 0 && i + static_cast<std::size_t>(extra) < s.size();
        for (int k = 1; whole && k <= extra; ++k) {
            const auto n = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            whole = (n & 0xc0) == 0x80;
            c = c << 6 | (n & 0x3f);
        }
        if (!whole) {
            out.push_back(u'\xfffd');  // not UTF-8: the replacement character
            ++i;
            continue;
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back(static_cast<char16_t>(0xd800 + (c >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 + (c & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
        i += 1 + static_cast<std::size_t>(extra);
    }
    return out;
}

std::string utf8_of(const std::u16string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | c >> 6));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | c >> 12));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | c >> 18));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

// A scene line's text in `language`, its bytes up to the NUL; false when the table's word is -1.
bool scene_text(const Script& s, std::uint32_t table, int language, std::string* out) {
    const std::uint32_t at = le32(s.tail, table + 4 * static_cast<std::size_t>(language));
    out->clear();
    if (at == 0xffffffffu)
        return false;
    for (std::size_t i = at; i < s.tail.size() && s.tail[i] != 0; ++i)
        out->push_back(static_cast<char>(s.tail[i]));
    return true;
}

}  // namespace

bool is_text_database(const std::string& game_path) {
    return game_path.size() > 3 && game_path.substr(game_path.size() - 3) == ".p2";
}

bool is_scene_archive(const std::string& game_path) {
    return game_path.rfind("ev/EV_", 0) == 0 && is_text_database(game_path);
}

bool parse_scene_file(const std::vector<std::uint8_t>& bytes, int language, TextFile* out, std::string* why) {
    if (language < 1 || language > 5)
        return fail_with(why, "no such language");
    TextFile t;
    t.scenes = true;
    t.language = language;
    if (!parse_p2(bytes, &t.p2, why))
        return false;
    for (std::size_t e = 0; e < t.p2.entries.size(); ++e) {
        Script s;
        std::string problem;
        if (!parse_script(t.p2.entries[e].data, &s, &problem))
            return fail_with(why, "scene " + std::to_string(e) + ": " + problem);
        std::vector<SceneLine> lines;
        std::vector<std::u16string> list;
        for (std::size_t i = 0; i < s.steps.size(); ++i) {
            if (raw_step(s.steps[i]))
                continue;
            for (std::size_t w = 0; w + 1 < s.steps[i].words.size(); w += 2) {
                const OperandSlot o = operand_at(s.steps[i], w);
                if (o.mode != 0x40)
                    continue;
                const auto table = static_cast<std::uint32_t>(o.value);
                // a table two steps show is one line
                if (std::any_of(lines.begin(), lines.end(), [&](const SceneLine& l) { return l.table == table; }))
                    continue;
                if (static_cast<std::size_t>(table) + 7 * 4 > s.tail.size())
                    return fail_with(why, "scene " + std::to_string(e) + ": a line's table past its data");
                std::string text;
                const std::uint32_t at = le32(s.tail, table + 4 * static_cast<std::size_t>(language));
                if (at != 0xffffffffu && at >= s.tail.size())
                    return fail_with(why, "scene " + std::to_string(e) + ": a line's text past its data");
                scene_text(s, table, language, &text);
                lines.push_back({i, w / 2, table});
                list.push_back(utf16_of(text));
            }
        }
        t.scripts.push_back(std::move(s));
        t.lines.push_back(std::move(lines));
        t.lists.push_back(std::move(list));
    }
    *out = std::move(t);
    return true;
}

bool parse_text_file(const std::vector<std::uint8_t>& bytes, bool database, TextFile* out, std::string* why) {
    TextFile t;
    t.database = database;
    if (database) {
        if (!parse_p2(bytes, &t.p2, why))
            return false;
        for (std::size_t e = 0; e < t.p2.entries.size(); ++e) {
            std::vector<std::u16string> list;
            std::string problem;
            if (!parse_list(t.p2.entries[e].data, &list, &problem))
                return fail_with(why, "list " + std::to_string(e) + ": " + problem);
            t.lists.push_back(std::move(list));
        }
        *out = std::move(t);
        return true;
    }
    std::vector<std::uint8_t> d = bytes;
    if (!d.empty() && (d[0] == 0x10 || d[0] == 0x11)) {
        t.lz = d[0];
        try {
            d = khdays::assets::lz_decompress(d);
        } catch (const std::exception& e) {
            return fail_with(why, std::string("its LZ cannot be undone: ") + e.what());
        }
    }
    if (d.size() < 8)
        return fail_with(why, "a string table shorter than its header");
    t.header_size = le32(d, 0);
    const std::uint32_t count = le32(d, 4);
    if (t.header_size < 8 || t.header_size > d.size())
        return fail_with(why, "a string table's header out of place");
    std::vector<std::u16string> list;
    std::size_t at = t.header_size;
    for (std::uint32_t k = 0; k < count; ++k) {
        if (at + 4 > d.size())
            return fail_with(why, "a string past the table");
        const std::size_t length = le32(d, at);
        if (length < 6 || at + length > d.size() || (length & 1) != 0)
            return fail_with(why, "a string's length out of place");
        std::u16string text;
        for (std::size_t b = at + 4; b + 2 <= at + length; b += 2)
            text.push_back(static_cast<char16_t>(d[b] | d[b + 1] << 8));
        if (text.back() != u'\0')
            return fail_with(why, "a string without its end mark");
        text.pop_back();
        list.push_back(std::move(text));
        at += length;
    }
    if (at != d.size())
        return fail_with(why, "bytes after the table's last string");
    t.lists.push_back(std::move(list));
    *out = std::move(t);
    return true;
}

std::vector<std::uint8_t> write_text_file(const TextFile& t) {
    if (t.scenes) {
        P2File p = t.p2;
        for (std::size_t e = 0; e < p.entries.size() && e < t.scripts.size() && e < t.lists.size(); ++e) {
            Script s = t.scripts[e];
            bool changed = false;
            for (std::size_t k = 0; k < t.lines[e].size() && k < t.lists[e].size(); ++k) {
                const SceneLine& line = t.lines[e][k];
                std::string now;
                const bool has = scene_text(s, line.table, t.language, &now);
                if (has ? utf16_of(now) == t.lists[e][k] : t.lists[e][k].empty())
                    continue;
                changed =
                    set_step_text(s, s.steps[line.step], line.slot, t.language, utf8_of(t.lists[e][k])) || changed;
            }
            if (changed)
                p.entries[e].data = write_script(s);
        }
        return write_p2(p);
    }
    if (t.database) {
        P2File p = t.p2;
        for (std::size_t e = 0; e < p.entries.size() && e < t.lists.size(); ++e)
            p.entries[e].data = write_list(t.lists[e]);
        return write_p2(p);
    }
    std::vector<std::uint8_t> d;
    add32(d, static_cast<std::uint32_t>(t.header_size));
    add32(d, static_cast<std::uint32_t>(t.lists.empty() ? 0 : t.lists[0].size()));
    while (d.size() < t.header_size)
        d.push_back(0);
    if (!t.lists.empty()) {
        for (const auto& s : t.lists[0]) {
            add32(d, static_cast<std::uint32_t>(4 + (s.size() + 1) * 2));
            for (const char16_t c : s)
                add16(d, c);
            add16(d, 0);
        }
    }
    return t.lz != 0 ? lz_compress(t.lz, d) : d;
}

std::u16string missing_glyphs(const std::u16string& text, const std::function<bool(char16_t)>& font_has) {
    std::u16string out;
    std::set<char16_t> seen;
    for (const char16_t c : text) {
        if (c < 0x20 || c == 0x7f)
            continue;  // control codes: the game's, not drawn
        if (!font_has(c) && seen.insert(c).second)
            out.push_back(c);
    }
    return out;
}

namespace {

// A scene archive read in each language and written back with nothing changed: each entry the same bytes;
// then each scene's first line changed in one language: read back changed there and the same in the others.
// The problems found, -1 when it cannot be read.
int check_scenes(const std::string& rel, const std::vector<std::uint8_t>& bytes,
                 const std::function<void(const std::string&)>& line) {
    std::vector<TextFile> by_language(6);
    std::string why;
    for (int l = 1; l <= 5; ++l) {
        if (!parse_scene_file(bytes, l, &by_language[static_cast<std::size_t>(l)], &why)) {
            line(rel + ": " + why);
            return -1;
        }
    }
    int problems = 0;
    const TextFile& en = by_language[1];
    for (std::size_t e = 0; e < en.p2.entries.size(); ++e) {
        if (write_script(en.scripts[e]) != en.p2.entries[e].data) {
            line(rel + ": scene " + std::to_string(e) + " written back differently");
            ++problems;
        }
    }
    P2File back;
    if (!parse_p2(write_text_file(en), &back, &why) || back.entries.size() != en.p2.entries.size()) {
        line(rel + ": written back, not read back" + (why.empty() ? std::string() : ": " + why));
        return problems + 1;
    }
    for (std::size_t e = 0; e < back.entries.size(); ++e) {
        if (back.entries[e].data != en.p2.entries[e].data) {
            line(rel + ": scene " + std::to_string(e) + " written back differently");
            ++problems;
        }
    }
    for (int l = 1; l <= 5; ++l) {
        TextFile changed = by_language[static_cast<std::size_t>(l)];
        for (auto& list : changed.lists) {
            if (!list.empty())
                list[0] = u"~" + list[0];
        }
        const std::vector<std::uint8_t> written = write_text_file(changed);
        for (int o = 1; o <= 5; ++o) {
            TextFile again;
            const auto& want = o == l ? changed.lists : by_language[static_cast<std::size_t>(o)].lists;
            if (!parse_scene_file(written, o, &again, &why) || again.lists != want) {
                line(rel + ": a line changed in language " + std::to_string(l) + " reads back wrong in " +
                     std::to_string(o));
                ++problems;
            }
        }
    }
    return problems;
}

}  // namespace

bool check_all_texts(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line) {
    namespace fs = std::filesystem;
    int files = 0, problems = 0;
    std::size_t strings = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(nitrofs, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file())
            continue;
        const std::string rel = u8(it->path().lexically_relative(nitrofs));
        const std::string name = u8(it->path().filename());
        const bool table = rel.size() > 4 && rel.substr(rel.size() - 4) == ".s.z";
        const bool database = rel.rfind("db/db_", 0) == 0 && name.size() == 8 && is_text_database(name);
        const bool scenes = is_scene_archive(rel);
        if (!table && !database && !scenes)
            continue;
        ++files;
        std::ifstream in(it->path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        TextFile t;
        std::string why;
        if (scenes) {
            const int found = check_scenes(rel, bytes, line);
            problems += found < 0 ? 1 : found;
            if (found >= 0 && parse_scene_file(bytes, 1, &t, &why)) {
                for (const auto& l : t.lists)
                    strings += l.size();
            }
            continue;
        }
        if (!parse_text_file(bytes, database, &t, &why)) {
            line(rel + ": " + why);
            ++problems;
            continue;
        }
        // written back: the same, once undone where packed (the packer's choices may differ)
        const std::vector<std::uint8_t> back = write_text_file(t);
        TextFile again;
        if (!parse_text_file(back, database, &again, &why) || again.lists != t.lists) {
            line(rel + ": written back, not read back the same" + (why.empty() ? std::string() : ": " + why));
            ++problems;
            continue;
        }
        if (!database) {
            std::vector<std::uint8_t> a = bytes, b = back;
            if (t.lz != 0) {
                a = khdays::assets::lz_decompress(a);
                b = khdays::assets::lz_decompress(b);
            }
            if (a != b) {
                line(rel + ": written back differently");
                ++problems;
                continue;
            }
        } else {
            for (std::size_t e = 0; e < t.p2.entries.size(); ++e) {
                if (again.p2.entries[e].data != t.p2.entries[e].data) {
                    line(rel + ": list " + std::to_string(e) + " written back differently");
                    ++problems;
                }
            }
        }
        for (const auto& l : t.lists)
            strings += l.size();
    }
    line(std::to_string(files) + " text files, " + std::to_string(strings) + " strings read and written back; " +
         std::to_string(problems) + " problems");
    return problems == 0;
}

}  // namespace studio
