#include "core/files/catalog.h"

#include "core/files/paths.h"
#include "core/rooms/worlds.h"
#include "core/texts/texts.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>

namespace studio {

namespace {

constexpr std::array<std::string_view, 6> kLanguages{"de", "en", "es", "fr", "it", "ja"};

bool is_language(std::string_view code) {
    return std::find(kLanguages.begin(), kLanguages.end(), code) != kLanguages.end();
}

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

// Every regular file under `dir`, as a path relative to it ('/' separators).
template <typename F>
void walk(const fs::path& dir, std::atomic<int>* progress, const std::atomic<bool>* cancel, F&& each) {
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (cancel != nullptr && cancel->load())
            return;
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec))
            continue;
        if (progress != nullptr)
            progress->fetch_add(1);
        each(it->path(), u8(it->path().lexically_relative(dir)));
    }
}

Item make(Kind kind, std::string key, std::string group, fs::path original, std::string language = {}) {
    Item item;
    item.kind = kind;
    item.key = std::move(key);
    item.group = std::move(group);
    item.original = std::move(original);
    item.language = std::move(language);
    item.folded = lower(item.key);
    return item;
}

}  // namespace

std::string text_language(std::string_view game_path) {
    const auto slash = game_path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? game_path : game_path.substr(slash + 1);
    for (std::size_t at = name.find('_'); at != std::string_view::npos; at = name.find('_', at + 1)) {
        if (at + 3 < name.size() && name[at + 3] == '.' && is_language(name.substr(at + 1, 2))) {
            return std::string(name.substr(at + 1, 2));
        }
    }
    std::size_t start = 0;
    while (slash != std::string_view::npos && start < slash) {
        const auto end = game_path.find('/', start);
        if (end == std::string_view::npos || end > slash)
            break;
        if (is_language(game_path.substr(start, end - start)))
            return std::string(game_path.substr(start, end - start));
        start = end + 1;
    }
    return {};
}

std::string any_palette_key(const Item& item) {
    if (item.kind != Kind::texture)
        return {};
    const auto tex = item.key.rfind("/tex/");
    const auto at = item.key.rfind('@');
    if (tex == std::string::npos || at == std::string::npos || at < tex)
        return {};
    return item.key.substr(0, at);
}

std::string export_name(const Item& item) {
    const auto last = [](std::string_view path) {
        const auto slash = path.find_last_of('/');
        return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
    };
    if (item.kind == Kind::sheet) {
        // `<file>/chr/<n>`: two languages' sheets of one name kept apart
        std::string name = last(item.group);
        const std::string language = text_language(item.group);
        if (!language.empty() && name.find("_" + language + ".") == std::string::npos)
            name += "_" + language;
        const std::string n = last(item.key);
        if (n != "0")
            name += "_" + n;
        return name + ".png";
    }
    if (item.kind == Kind::texture)  // `<file>/tex/<texture>@<palette>`
        return last(item.key) + ".png";
    if (item.kind == Kind::room)  // `<world file>/room/<index>`
        return last(item.group) + "_room" + last(item.key) + ".gltf";
    return last(item.key) + ".gltf";  // a model: `<file>/mdl/<name>`
}

std::vector<std::uint8_t> read_all_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Catalog build_catalog(const CatalogSources& sources, std::atomic<int>* progress, const std::atomic<bool>* cancel) {
    Catalog c;
    std::error_code ec;

    // The graphics dump: <file>[#<entry>]/tex|chr|mdl/<name>.png|.gltf
    if (!sources.graphics_dump.empty() && fs::is_directory(sources.graphics_dump, ec)) {
        c.graphics_dump_found = true;
        walk(sources.graphics_dump, progress, cancel, [&](const fs::path& path, const std::string& rel) {
            const auto slash = rel.find_last_of('/');
            if (slash == std::string::npos)
                return;
            const auto folder_at = rel.find_last_of('/', slash - 1);
            if (folder_at == std::string::npos)
                return;
            const std::string folder = rel.substr(folder_at + 1, slash - folder_at - 1);
            const std::string group = rel.substr(0, folder_at);
            const std::string ext = lower(u8(path.extension()));
            const std::string key = rel.substr(0, rel.size() - ext.size());
            if (folder == "tex" && ext == ".png")
                c.items.push_back(make(Kind::texture, key, group, path));
            else if (folder == "chr" && ext == ".png")
                c.items.push_back(make(Kind::sheet, key, group, path));
            else if (folder == "mdl" && ext == ".gltf")
                c.items.push_back(make(Kind::model, key, group, path));
        });
    }

    // The sounds dump: seq|seqarc|strm|wave/.../<name>.wav
    if (!sources.sounds_dump.empty() && fs::is_directory(sources.sounds_dump, ec)) {
        c.sounds_dump_found = true;
        walk(sources.sounds_dump, progress, cancel, [&](const fs::path& path, const std::string& rel) {
            const std::string ext = lower(u8(path.extension()));
            if (ext != ".wav")
                return;
            const auto first = rel.find('/');
            if (first == std::string::npos)
                return;
            const std::string kind = rel.substr(0, first);
            if (kind != "seq" && kind != "seqarc" && kind != "strm" && kind != "wave")
                return;
            const auto slash = rel.find_last_of('/');
            c.items.push_back(make(Kind::sound, rel.substr(0, rel.size() - ext.size()), rel.substr(0, slash), path));
        });
    }

    // The game's files: its texts and movies.
    if (!sources.nitrofs.empty() && fs::is_directory(sources.nitrofs, ec)) {
        c.game_files_found = true;
        walk(sources.nitrofs, progress, cancel, [&](const fs::path& path, const std::string& rel) {
            const auto slash = rel.find_last_of('/');
            const std::string group = slash == std::string::npos ? std::string() : rel.substr(0, slash);
            const std::string name = slash == std::string::npos ? rel : rel.substr(slash + 1);
            if (ends_with(rel, ".s.z")) {
                c.items.push_back(make(Kind::text_file, rel, group, path, text_language(rel)));
            } else if (group == "db" && name.size() == 8 && name.rfind("db_", 0) == 0 && ends_with(name, ".p2") &&
                       is_language(name.substr(3, 2))) {
                // db/db.p2 is not known to hold text: left out
                c.items.push_back(make(Kind::text_file, rel, group, path, name.substr(3, 2)));
            } else if (is_scene_archive(rel)) {
                // the story's scenes, every language in one file: listed when they have lines
                TextFile t;
                if (parse_scene_file(read_all_bytes(path), 1, &t, nullptr) &&
                    std::any_of(t.lists.begin(), t.lists.end(), [](const auto& l) { return !l.empty(); })) {
                    c.items.push_back(make(Kind::text_file, rel, group, path));
                }
            } else if (group == "mv" && ends_with(name, ".mods")) {
                c.items.push_back(make(Kind::movie, name.substr(0, name.size() - 5), group, path));
            } else if (group == "mi/mi" && !name.empty() &&
                       std::all_of(name.begin(), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
                // a mission's file: its scripts
                c.items.push_back(make(Kind::mission, rel, group, path));
            } else if (group == "mi/wd" && name.rfind("wd_", 0) == 0) {
                // a mission world's rooms, by their place in its room table
                World w;
                if (parse_world(read_all_bytes(path), name.substr(3), &w, nullptr)) {
                    for (const auto& r : w.rooms) {
                        c.items.push_back(make(Kind::room, rel + "/room/" + std::to_string(r.index), rel, path));
                    }
                }
            }
        });
    }

    std::sort(c.items.begin(), c.items.end(), [](const Item& a, const Item& b) {
        if (a.kind != b.kind)
            return a.kind < b.kind;
        return a.key < b.key;
    });
    return c;
}

std::vector<int> Catalog::search(std::string_view query, unsigned kinds) const {
    std::vector<std::string> words;
    {
        std::istringstream in{lower(query)};
        std::string w;
        while (in >> w)
            words.push_back(w);
    }
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const Item& item = items[static_cast<std::size_t>(i)];
        if ((kinds & (1u << static_cast<unsigned>(item.kind))) == 0)
            continue;
        bool all = true;
        for (const auto& w : words) {
            if (item.folded.find(w) == std::string::npos) {
                all = false;
                break;
            }
        }
        if (all)
            out.push_back(i);
    }
    return out;
}

}  // namespace studio
